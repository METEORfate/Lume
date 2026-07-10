#include "lume/connection.h"

#include "lume/buffer.h"
#include "lume/event_loop.h"
#include "lume/http.h"
#include "lume/log.h"
#include "lume/static_file.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <time.h>
#include <unistd.h>

#define LUME_READ_CHUNK_SIZE 4096
#define LUME_FILE_CHUNK_SIZE 16384

typedef enum connection_state {
    CONNECTION_READING,
    CONNECTION_PROCESSING,
    CONNECTION_WRITING_HEADER,
    CONNECTION_WRITING_FILE,
    CONNECTION_CLOSING
} connection_state;

typedef enum io_result {
    IO_DONE,
    IO_BLOCKED,
    IO_CLOSED,
    IO_ERROR
} io_result;

struct lume_connection {
    lume_event_source source;
    int fd;
    int epoll_fd;
    const lume_config *config;
    connection_state state;
    time_t last_active_at;
    lume_buffer read_buffer;
    lume_buffer header_buffer;
    int file_fd;
    off_t file_size;
    off_t file_sent;
    char file_chunk[LUME_FILE_CHUNK_SIZE];
    size_t file_chunk_length;
    size_t file_chunk_offset;
};

static void mark_active(lume_connection *connection)
{
    connection->last_active_at = time(NULL);
}

static uint32_t events_for_state(connection_state state)
{
    switch (state) {
    case CONNECTION_READING:
        return EPOLLIN | EPOLLRDHUP;
    case CONNECTION_WRITING_HEADER:
    case CONNECTION_WRITING_FILE:
        return EPOLLOUT | EPOLLRDHUP;
    case CONNECTION_PROCESSING:
    case CONNECTION_CLOSING:
    default:
        return 0;
    }
}

static int update_events(lume_connection *connection)
{
    struct epoll_event event;
    uint32_t events = events_for_state(connection->state);

    if (events == 0) {
        return 0;
    }

    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.ptr = connection;

    if (epoll_ctl(connection->epoll_fd, EPOLL_CTL_MOD, connection->fd, &event) != 0) {
        LUME_LOGE("epoll_ctl mod client fd=%d failed: %s", connection->fd, strerror(errno));
        return -1;
    }

    return 0;
}

static int set_state(lume_connection *connection, connection_state state)
{
    connection->state = state;
    return update_events(connection);
}

lume_connection *lume_connection_create(int fd, int epoll_fd, const lume_config *config)
{
    lume_connection *connection = calloc(1, sizeof(*connection));

    if (!connection) {
        return NULL;
    }

    connection->source.type = LUME_EVENT_CONNECTION;
    connection->fd = fd;
    connection->epoll_fd = epoll_fd;
    connection->config = config;
    connection->state = CONNECTION_READING;
    connection->last_active_at = time(NULL);
    connection->file_fd = -1;
    connection->file_size = 0;
    connection->file_sent = 0;
    lume_buffer_init(&connection->read_buffer);
    lume_buffer_init(&connection->header_buffer);

    return connection;
}

void lume_connection_destroy(lume_connection *connection)
{
    if (!connection) {
        return;
    }

    if (connection->epoll_fd >= 0 && connection->fd >= 0) {
        epoll_ctl(connection->epoll_fd, EPOLL_CTL_DEL, connection->fd, NULL);
    }

    if (connection->file_fd >= 0) {
        close(connection->file_fd);
        connection->file_fd = -1;
    }

    if (connection->fd >= 0) {
        close(connection->fd);
        connection->fd = -1;
    }

    lume_buffer_free(&connection->read_buffer);
    lume_buffer_free(&connection->header_buffer);
    free(connection);
}

int lume_connection_register(lume_connection *connection)
{
    struct epoll_event event;

    if (!connection) {
        return -1;
    }

    memset(&event, 0, sizeof(event));
    event.events = events_for_state(connection->state);
    event.data.ptr = connection;

    if (epoll_ctl(connection->epoll_fd, EPOLL_CTL_ADD, connection->fd, &event) != 0) {
        LUME_LOGE("epoll_ctl add client fd=%d failed: %s", connection->fd, strerror(errno));
        return -1;
    }

    return 0;
}

int lume_connection_is_idle_expired(const lume_connection *connection, time_t now)
{
    unsigned int timeout;

    if (!connection || !connection->config) {
        return 0;
    }

    timeout = connection->config->request_timeout_seconds;
    if (timeout == 0) {
        return 0;
    }

    return now >= connection->last_active_at &&
           (unsigned int)(now - connection->last_active_at) >= timeout;
}

static int request_headers_complete(const lume_buffer *buffer)
{
    size_t i;

    if (!buffer || !buffer->data) {
        return 0;
    }

    for (i = 0; i < buffer->length; i++) {
        if (i + 3 < buffer->length &&
            buffer->data[i] == '\r' &&
            buffer->data[i + 1] == '\n' &&
            buffer->data[i + 2] == '\r' &&
            buffer->data[i + 3] == '\n') {
            return 1;
        }

        if (i + 1 < buffer->length &&
            buffer->data[i] == '\n' &&
            buffer->data[i + 1] == '\n') {
            return 1;
        }
    }

    return 0;
}

static int prepare_error_response(lume_connection *connection, lume_http_status status)
{
    if (connection->file_fd >= 0) {
        close(connection->file_fd);
        connection->file_fd = -1;
    }

    lume_buffer_clear(&connection->header_buffer);
    connection->file_size = 0;
    connection->file_sent = 0;
    connection->file_chunk_length = 0;
    connection->file_chunk_offset = 0;

    if (lume_http_append_error_response(&connection->header_buffer, status) != 0) {
        return -1;
    }

    return set_state(connection, CONNECTION_WRITING_HEADER);
}

static int prepare_static_response(lume_connection *connection, const char *uri)
{
    lume_static_file file;
    lume_static_result result;

    lume_static_file_init(&file);
    result = lume_static_file_open(connection->config->root_dir, uri, &file);

    if (result == LUME_STATIC_BAD_REQUEST) {
        return prepare_error_response(connection, LUME_HTTP_BAD_REQUEST);
    }

    if (result == LUME_STATIC_NOT_FOUND) {
        return prepare_error_response(connection, LUME_HTTP_NOT_FOUND);
    }

    lume_buffer_clear(&connection->header_buffer);
    if (lume_http_append_response_headers(&connection->header_buffer,
                                          LUME_HTTP_OK,
                                          file.content_type,
                                          (uintmax_t)file.size) != 0) {
        lume_static_file_close(&file);
        return -1;
    }

    connection->file_fd = file.fd;
    connection->file_size = file.size;
    connection->file_sent = 0;
    connection->file_chunk_length = 0;
    connection->file_chunk_offset = 0;
    file.fd = -1;

    return set_state(connection, CONNECTION_WRITING_HEADER);
}

static int process_request(lume_connection *connection)
{
    lume_http_request request;
    lume_http_parse_result result;

    result = lume_http_parse_request(connection->read_buffer.data,
                                     connection->read_buffer.length,
                                     &request);

    if (result == LUME_HTTP_PARSE_INCOMPLETE) {
        return set_state(connection, CONNECTION_READING);
    }

    if (result == LUME_HTTP_PARSE_BAD_REQUEST) {
        return prepare_error_response(connection, LUME_HTTP_BAD_REQUEST);
    }

    if (request.method != LUME_HTTP_METHOD_GET) {
        return prepare_error_response(connection, LUME_HTTP_NOT_IMPLEMENTED);
    }

    return prepare_static_response(connection, request.uri);
}

static int transition_to_processing(lume_connection *connection)
{
    connection->state = CONNECTION_PROCESSING;
    return process_request(connection);
}

static io_result read_request(lume_connection *connection)
{
    for (;;) {
        char chunk[LUME_READ_CHUNK_SIZE];
        ssize_t received = read(connection->fd, chunk, sizeof(chunk));

        if (received > 0) {
            mark_active(connection);

            if (lume_buffer_append(&connection->read_buffer, chunk, (size_t)received) != 0) {
                return IO_ERROR;
            }

            if (connection->read_buffer.length > connection->config->max_request_bytes) {
                if (prepare_error_response(connection, LUME_HTTP_BAD_REQUEST) != 0) {
                    return IO_ERROR;
                }
                return IO_DONE;
            }

            if (request_headers_complete(&connection->read_buffer)) {
                if (transition_to_processing(connection) != 0) {
                    return IO_ERROR;
                }
                return IO_DONE;
            }

            continue;
        }

        if (received == 0) {
            return IO_CLOSED;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return IO_BLOCKED;
        }

        LUME_LOGW("read client fd=%d failed: %s", connection->fd, strerror(errno));
        return IO_ERROR;
    }
}

static io_result flush_header(lume_connection *connection)
{
    while (lume_buffer_remaining(&connection->header_buffer) > 0) {
        ssize_t sent = write(connection->fd,
                             lume_buffer_current(&connection->header_buffer),
                             lume_buffer_remaining(&connection->header_buffer));

        if (sent > 0) {
            mark_active(connection);
            lume_buffer_advance(&connection->header_buffer, (size_t)sent);
            continue;
        }

        if (sent == 0) {
            return IO_BLOCKED;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return IO_BLOCKED;
        }

        LUME_LOGW("write response header to fd=%d failed: %s", connection->fd, strerror(errno));
        return IO_ERROR;
    }

    return IO_DONE;
}

static io_result load_next_file_chunk(lume_connection *connection)
{
    off_t remaining = connection->file_size - connection->file_sent;
    size_t read_limit;

    if (remaining <= 0) {
        return IO_CLOSED;
    }

    read_limit = sizeof(connection->file_chunk);
    if ((off_t)read_limit > remaining) {
        read_limit = (size_t)remaining;
    }

    for (;;) {
        ssize_t read_bytes = read(connection->file_fd,
                                  connection->file_chunk,
                                  read_limit);

        if (read_bytes > 0) {
            connection->file_chunk_length = (size_t)read_bytes;
            connection->file_chunk_offset = 0;
            return IO_DONE;
        }

        if (read_bytes == 0) {
            return IO_CLOSED;
        }

        if (errno == EINTR) {
            continue;
        }

        LUME_LOGW("read static file failed: %s", strerror(errno));
        return IO_ERROR;
    }
}

static io_result flush_file_chunk(lume_connection *connection)
{
    while (connection->file_chunk_offset < connection->file_chunk_length) {
        ssize_t sent = write(connection->fd,
                             connection->file_chunk + connection->file_chunk_offset,
                             connection->file_chunk_length - connection->file_chunk_offset);

        if (sent > 0) {
            mark_active(connection);
            connection->file_chunk_offset += (size_t)sent;
            connection->file_sent += (off_t)sent;
            continue;
        }

        if (sent == 0) {
            return IO_BLOCKED;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return IO_BLOCKED;
        }

        LUME_LOGW("write file chunk to fd=%d failed: %s", connection->fd, strerror(errno));
        return IO_ERROR;
    }

    return IO_DONE;
}

static io_result send_file(lume_connection *connection)
{
    while (connection->file_fd >= 0) {
        io_result result;

        if (connection->file_chunk_offset >= connection->file_chunk_length) {
            result = load_next_file_chunk(connection);
            if (result == IO_CLOSED) {
                close(connection->file_fd);
                connection->file_fd = -1;
                return IO_DONE;
            }
            if (result != IO_DONE) {
                return result;
            }
        }

        result = flush_file_chunk(connection);
        if (result != IO_DONE) {
            return result;
        }
    }

    return IO_DONE;
}

static int handle_read(lume_connection *connection)
{
    io_result result = read_request(connection);

    if (result == IO_CLOSED || result == IO_ERROR) {
        return 1;
    }

    return 0;
}

static int handle_write_file(lume_connection *connection);

static int handle_write_header(lume_connection *connection)
{
    io_result result = flush_header(connection);

    if (result == IO_ERROR || result == IO_CLOSED) {
        return 1;
    }

    if (result == IO_BLOCKED) {
        return 0;
    }

    if (connection->file_fd >= 0) {
        if (set_state(connection, CONNECTION_WRITING_FILE) != 0) {
            return 1;
        }
        return handle_write_file(connection);
    }

    connection->state = CONNECTION_CLOSING;
    return 1;
}

static int handle_write_file(lume_connection *connection)
{
    io_result result = send_file(connection);

    if (result == IO_ERROR || result == IO_CLOSED) {
        return 1;
    }

    if (result == IO_BLOCKED) {
        return 0;
    }

    connection->state = CONNECTION_CLOSING;
    return 1;
}

int lume_connection_handle(lume_connection *connection, uint32_t events)
{
    if (!connection) {
        return 1;
    }

    if (events & (EPOLLERR | EPOLLHUP)) {
        return 1;
    }

    if ((events & EPOLLRDHUP) && !(events & (EPOLLIN | EPOLLOUT))) {
        return 1;
    }

    switch (connection->state) {
    case CONNECTION_READING:
        if (events & EPOLLIN) {
            return handle_read(connection);
        }
        break;
    case CONNECTION_PROCESSING:
        return transition_to_processing(connection) != 0;
    case CONNECTION_WRITING_HEADER:
        if (events & EPOLLOUT) {
            return handle_write_header(connection);
        }
        break;
    case CONNECTION_WRITING_FILE:
        if (events & EPOLLOUT) {
            return handle_write_file(connection);
        }
        break;
    case CONNECTION_CLOSING:
    default:
        return 1;
    }

    return 0;
}
