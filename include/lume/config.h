#ifndef LUME_CONFIG_H
#define LUME_CONFIG_H

#include <limits.h>
#include <stddef.h>

typedef struct lume_config {
    int port;
    char root_dir[PATH_MAX];
} lume_config;

void lume_config_init(lume_config *config);
int lume_config_load(const char *path, lume_config *config, char *error, size_t error_len);

#endif
