#include "lume/event_loop.h"

#include "lume/connection.h"
#include "lume/log.h"
#include "lume/server.h"

#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

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

static void close_connection(lume_server *server, lume_connection *connection)
{
    lume_connection_destroy(connection);
    if (server->active_connections > 0) {
        server->active_connections--;
    }
}

static void accept_connections(lume_server *server)
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

        connection = lume_connection_create(client_fd, server->epoll_fd, &server->config);
        if (!connection) {
            close(client_fd);
            continue;
        }

        if (lume_connection_register(connection) != 0) {
            lume_connection_destroy(connection);
            continue;
        }

        server->active_connections++;
    }
}

int lume_event_loop_run(lume_server *server)
{
    struct epoll_event events[LUME_DEFAULT_MAX_EVENTS];
    lume_event_source listener = {LUME_EVENT_LISTENER};

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
        int count = epoll_wait(server->epoll_fd, events, LUME_DEFAULT_MAX_EVENTS, -1);
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
                accept_connections(server);
            } else if (source->type == LUME_EVENT_CONNECTION) {
                lume_connection *connection = (lume_connection *)source;
                if (lume_connection_handle(connection, events[i].events) != 0) {
                    close_connection(server, connection);
                }
            }
        }
    }
}
