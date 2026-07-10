#ifndef LUME_CONFIG_H
#define LUME_CONFIG_H

#include <limits.h>
#include <stddef.h>

#define LUME_DEFAULT_MAX_REQUEST_BYTES 16384
#define LUME_DEFAULT_MAX_CONNECTIONS 4096
#define LUME_DEFAULT_REQUEST_TIMEOUT_SECONDS 30

typedef struct lume_config {
    int port;
    size_t max_request_bytes;
    size_t max_connections;
    unsigned int request_timeout_seconds;
    char root_dir[PATH_MAX];
} lume_config;

void lume_config_init(lume_config *config);
int lume_config_load(const char *path, lume_config *config, char *error, size_t error_len);

#endif
