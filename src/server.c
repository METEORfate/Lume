#include "lume/server.h"

#include "lume/event_loop.h"
#include "lume/log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int lume_set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0) {
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int lume_server_init(lume_server *server, const lume_config *config)
{
    int fd;
    int reuse = 1;
    struct sockaddr_in addr;

    if (!server || !config) {
        return -1;
    }

    memset(server, 0, sizeof(*server));
    server->config = *config;
    server->listen_fd = -1;
    server->epoll_fd = -1;

    fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        LUME_LOGE("socket failed: %s", strerror(errno));
        return -1;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        LUME_LOGE("setsockopt(SO_REUSEADDR) failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)config->port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        LUME_LOGE("bind port %d failed: %s", config->port, strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, LUME_DEFAULT_BACKLOG) != 0) {
        LUME_LOGE("listen failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    server->listen_fd = fd;
    LUME_LOGI("listening on port %d, root=%s", config->port, config->root_dir);
    LUME_LOGI("limits: max_request_bytes=%zu, max_connections=%zu, request_timeout=%us",
              config->max_request_bytes,
              config->max_connections,
              config->request_timeout_seconds);
    return 0;
}

int lume_server_run(lume_server *server)
{
    return lume_event_loop_run(server);
}

void lume_server_destroy(lume_server *server)
{
    if (!server) {
        return;
    }

    if (server->epoll_fd >= 0) {
        close(server->epoll_fd);
        server->epoll_fd = -1;
    }

    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
    }
}
