#ifndef LUME_SERVER_H
#define LUME_SERVER_H

#include <stddef.h>

#include "lume/config.h"

#define LUME_DEFAULT_BACKLOG 128
#define LUME_DEFAULT_MAX_EVENTS 1024
#define LUME_EPOLL_WAIT_TIMEOUT_MS 1000

typedef struct lume_server {
    lume_config config;
    int listen_fd;
    int epoll_fd;
    size_t active_connections;
} lume_server;

int lume_set_nonblocking(int fd);
int lume_server_init(lume_server *server, const lume_config *config);
int lume_server_run(lume_server *server);
void lume_server_destroy(lume_server *server);

#endif
