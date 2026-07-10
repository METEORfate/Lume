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
#include <unistd.h>

#define LUME_MAX_REQUEST_BYTES 16384
#define LUME_READ_CHUNK_SIZE 4096
#define LUME_FILE_CHUNK_SIZE 16384

typedef enum connection_state {
    CONNECTION_READING,
    CONNECTION_WRITING,
    CONNECTION_CLOSING
} connection_state;

struct lume_connection {
    lume_event_source source;
    int fd;
    int epoll_fd;
    const lume_config *config;
    connection_state state;
    lume_buffer read_buffer;
    lume_buffer write_buffer;
    int file_fd;
    off_t file_size;
    char file_chunk[LUME_FILE_CHUNK_SIZE];
    size_t file_chunk_length;
    size_t file_chunk_offset;
};

static int update_events(lume_connection *connection, uint32_t events)
{
    struct epoll_event event;

    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.ptr = connection;

    return epoll_ctl(connection->epoll_fd, EPOLL_CTL_MOD, connection->fd, &event);
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
    connection->file_fd = -1;
    connection->file_size = 0;
    lume_buffer_init(&connection->read_buffer);
    lume_buffer_init(&connection->write_buffer);

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
    lume_buffer_free(&connection->write_buffer);
    free(connection);
}

int lume_connection_register(lume_connection *connection)
{
    struct epoll_event event;

    if (!connection) {
        return -1;
    }

    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.ptr = connection;

    if (epoll_ctl(connection->epoll_fd, EPOLL_CTL_ADD, connection->fd, &event) != 0) {
        LUME_LOGE("epoll_ctl client failed: %s", strerror(errno));
        return -1;
    }

    return 0;
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
    lume_buffer_clear(&connection->write_buffer);

    if (lume_http_append_error_response(&connection->write_buffer, status) != 0) {
        return -1;
    }

    connection->state = CONNECTION_WRITING;
    if (update_events(connection, EPOLLOUT) != 0) {
        return -1;
    }

    return 0;
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

    if (lume_http_append_response_headers(&connection->write_buffer,
                                          LUME_HTTP_OK,
                                          file.content_type,
                                          (uintmax_t)file.size) != 0) {
        lume_static_file_close(&file);
        return -1;
    }

    connection->file_fd = file.fd;
    connection->file_size = file.size;
    file.fd = -1;
    connection->state = CONNECTION_WRITING;

    if (update_events(connection, EPOLLOUT) != 0) {
        return -1;
    }

    return 0;
}

static int prepare_response(lume_connection *connection)
{
    lume_http_request request;
    lume_http_parse_result result;

    result = lume_http_parse_request(connection->read_buffer.data,
                                     connection->read_buffer.length,
                                     &request);

    if (result == LUME_HTTP_PARSE_INCOMPLETE) {
        return 0;
    }

    if (result == LUME_HTTP_PARSE_BAD_REQUEST) {
        return prepare_error_response(connection, LUME_HTTP_BAD_REQUEST);
    }

    if (request.method != LUME_HTTP_METHOD_GET) {
        return prepare_error_response(connection, LUME_HTTP_NOT_IMPLEMENTED);
    }

    return prepare_static_response(connection, request.uri);
}

static int handle_read(lume_connection *connection)
{
    for (;;) {
        char chunk[LUME_READ_CHUNK_SIZE];
        ssize_t received = read(connection->fd, chunk, sizeof(chunk));

        if (received > 0) {
            if (lume_buffer_append(&connection->read_buffer, chunk, (size_t)received) != 0) {
                return 1;
            }

            if (connection->read_buffer.length > LUME_MAX_REQUEST_BYTES) {
                return prepare_error_response(connection, LUME_HTTP_BAD_REQUEST) != 0;
            }

            if (request_headers_complete(&connection->read_buffer)) {
                return prepare_response(connection) != 0;
            }

            continue;
        }

        if (received == 0) {
            return 1;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }

        return 1;
    }
}

static int flush_buffer(lume_connection *connection, lume_buffer *buffer)
{
    while (lume_buffer_remaining(buffer) > 0) {
        ssize_t sent = write(connection->fd,
                             lume_buffer_current(buffer),
                             lume_buffer_remaining(buffer));

        if (sent > 0) {
            lume_buffer_advance(buffer, (size_t)sent);
            continue;
        }

        if (sent == 0) {
            return 0;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }

        return -1;
    }

    return 1;
}

static int load_next_file_chunk(lume_connection *connection)
{
    for (;;) {
        ssize_t read_bytes = read(connection->file_fd,
                                  connection->file_chunk,
                                  sizeof(connection->file_chunk));

        if (read_bytes > 0) {
            connection->file_chunk_length = (size_t)read_bytes;
            connection->file_chunk_offset = 0;
            return 0;
        }

        if (read_bytes == 0) {
            return 1;
        }

        if (errno == EINTR) {
            continue;
        }

        return -1;
    }
}

static int flush_file_chunk(lume_connection *connection)
{
    while (connection->file_chunk_offset < connection->file_chunk_length) {
        ssize_t sent = write(connection->fd,
                             connection->file_chunk + connection->file_chunk_offset,
                             connection->file_chunk_length - connection->file_chunk_offset);

        if (sent > 0) {
            connection->file_chunk_offset += (size_t)sent;
            continue;
        }

        if (sent == 0) {
            return 0;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }

        return -1;
    }

    return 1;
}

static int handle_write(lume_connection *connection)
{
    int result;

    result = flush_buffer(connection, &connection->write_buffer);
    if (result < 0) {
        return 1;
    }
    if (result == 0) {
        return 0;
    }

    while (connection->file_fd >= 0) {
        if (connection->file_chunk_offset >= connection->file_chunk_length) {
            result = load_next_file_chunk(connection);
            if (result < 0) {
                return 1;
            }
            if (result == 1) {
                return 1;
            }
        }

        result = flush_file_chunk(connection);
        if (result < 0) {
            return 1;
        }
        if (result == 0) {
            return 0;
        }
    }

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

    if (connection->state == CONNECTION_READING && (events & EPOLLIN)) {
        return handle_read(connection);
    }

    if (connection->state == CONNECTION_WRITING && (events & EPOLLOUT)) {
        return handle_write(connection);
    }

    if (connection->state == CONNECTION_CLOSING) {
        return 1;
    }

    return 0;
}
