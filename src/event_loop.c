#include "lume/event_loop.h"

#include "lume/connection.h"
#include "lume/log.h"
#include "lume/server.h"

#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

typedef struct connection_node {
    lume_connection *connection;
    struct connection_node *next;
} connection_node;

static int register_listener(lume_server *server, lume_event_source *listener)
{
    struct epoll_event event;

    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.ptr = listener;

    if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, server->listen_fd, &event) != 0) {
        LUME_LOGE("epoll_ctl listener failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static int track_connection(connection_node **connections, lume_connection *connection)
{
    connection_node *node = malloc(sizeof(*node));

    if (!node) {
        return -1;
    }

    node->connection = connection;
    node->next = *connections;
    *connections = node;
    return 0;
}

static void untrack_connection(connection_node **connections, lume_connection *connection)
{
    connection_node **current = connections;

    while (*current) {
        if ((*current)->connection == connection) {
            connection_node *matched = *current;
            *current = matched->next;
            free(matched);
            return;
        }
        current = &(*current)->next;
    }
}

static void close_connection(lume_server *server,
                             connection_node **connections,
                             lume_connection *connection)
{
    untrack_connection(connections, connection);
    lume_connection_destroy(connection);
    if (server->active_connections > 0) {
        server->active_connections--;
    }
}

static void accept_connections(lume_server *server, connection_node **connections)
{
    for (;;) {
        int client_fd = accept4(server->listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        lume_connection *connection;

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            LUME_LOGE("accept failed: %s", strerror(errno));
            return;
        }

        if (server->active_connections >= server->config.max_connections) {
            LUME_LOGW("max connections reached, closing accepted client fd=%d", client_fd);
            close(client_fd);
            continue;
        }

        connection = lume_connection_create(client_fd, server->epoll_fd, &server->config);
        if (!connection) {
            close(client_fd);
            continue;
        }

        if (lume_connection_register(connection) != 0) {
            lume_connection_destroy(connection);
            continue;
        }

        if (track_connection(connections, connection) != 0) {
            LUME_LOGE("failed to track client connection");
            lume_connection_destroy(connection);
            continue;
        }

        server->active_connections++;
    }
}

static void prune_idle_connections(lume_server *server, connection_node **connections)
{
    connection_node *node = *connections;
    time_t now;

    if (server->config.request_timeout_seconds == 0) {
        return;
    }

    now = time(NULL);
    while (node) {
        connection_node *next = node->next;

        if (lume_connection_is_idle_expired(node->connection, now)) {
            LUME_LOGW("closing idle connection after %us", server->config.request_timeout_seconds);
            close_connection(server, connections, node->connection);
        }

        node = next;
    }
}

int lume_event_loop_run(lume_server *server)
{
    struct epoll_event events[LUME_DEFAULT_MAX_EVENTS];
    lume_event_source listener = {LUME_EVENT_LISTENER};
    connection_node *connections = NULL;

    if (!server || server->listen_fd < 0) {
        return -1;
    }

    server->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (server->epoll_fd < 0) {
        LUME_LOGE("epoll_create1 failed: %s", strerror(errno));
        return -1;
    }

    if (register_listener(server, &listener) != 0) {
        return -1;
    }

    for (;;) {
        int count = epoll_wait(server->epoll_fd,
                               events,
                               LUME_DEFAULT_MAX_EVENTS,
                               LUME_EPOLL_WAIT_TIMEOUT_MS);
        int i;

        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            LUME_LOGE("epoll_wait failed: %s", strerror(errno));
            return -1;
        }

        for (i = 0; i < count; i++) {
            lume_event_source *source = events[i].data.ptr;

            if (!source) {
                continue;
            }

            if (source->type == LUME_EVENT_LISTENER) {
                accept_connections(server, &connections);
            } else if (source->type == LUME_EVENT_CONNECTION) {
                lume_connection *connection = (lume_connection *)source;
                if (lume_connection_handle(connection, events[i].events) != 0) {
                    close_connection(server, &connections, connection);
                }
            }
        }

        prune_idle_connections(server, &connections);
    }
}
