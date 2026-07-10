#include "lume/config.h"
#include "lume/log.h"
#include "lume/server.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    const char *config_path = argc > 1 ? argv[1] : "config/server.conf";
    char error[256];
    lume_config config;
    lume_server server;
    int result;

    signal(SIGPIPE, SIG_IGN);

    if (lume_config_load(config_path, &config, error, sizeof(error)) != 0) {
        LUME_LOGE("%s", error);
        return EXIT_FAILURE;
    }

    if (lume_server_init(&server, &config) != 0) {
        return EXIT_FAILURE;
    }

    result = lume_server_run(&server);
    lume_server_destroy(&server);
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
