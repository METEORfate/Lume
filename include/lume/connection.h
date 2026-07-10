#ifndef LUME_CONNECTION_H
#define LUME_CONNECTION_H

#include <stdint.h>
#include <time.h>

#include "lume/config.h"

typedef struct lume_connection lume_connection;

lume_connection *lume_connection_create(int fd, int epoll_fd, const lume_config *config);
void lume_connection_destroy(lume_connection *connection);
int lume_connection_register(lume_connection *connection);
int lume_connection_handle(lume_connection *connection, uint32_t events);
int lume_connection_is_idle_expired(const lume_connection *connection, time_t now);

#endif
