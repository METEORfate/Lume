#include "lume/config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_config(const char *path, const char *root, const char *port)
{
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp, "# generated test config\nPORT=%s\nROOT_DIR=%s\n", port, root);
    fclose(fp);
}

static void write_phase2_config(const char *path, const char *root)
{
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp,
            "# generated phase2 test config\n"
            "PORT=18081\n"
            "ROOT_DIR=%s\n"
            "MAX_REQUEST_BYTES=8192\n"
            "MAX_CONNECTIONS=64\n"
            "REQUEST_TIMEOUT=3\n",
            root);
    fclose(fp);
}

int main(void)
{
    char dir_template[] = "/tmp/lume_config_test_XXXXXX";
    char *dir = mkdtemp(dir_template);
    char config_path[PATH_MAX];
    char error[256];
    lume_config config;

    assert(dir != NULL);
    snprintf(config_path, sizeof(config_path), "%s/server.conf", dir);

    write_config(config_path, dir, "18080");
    assert(lume_config_load(config_path, &config, error, sizeof(error)) == 0);
    assert(config.port == 18080);
    assert(config.max_request_bytes == LUME_DEFAULT_MAX_REQUEST_BYTES);
    assert(config.max_connections == LUME_DEFAULT_MAX_CONNECTIONS);
    assert(config.request_timeout_seconds == LUME_DEFAULT_REQUEST_TIMEOUT_SECONDS);
    assert(strcmp(config.root_dir, dir) == 0);

    write_phase2_config(config_path, dir);
    assert(lume_config_load(config_path, &config, error, sizeof(error)) == 0);
    assert(config.port == 18081);
    assert(config.max_request_bytes == 8192);
    assert(config.max_connections == 64);
    assert(config.request_timeout_seconds == 3);

    write_config(config_path, dir, "70000");
    assert(lume_config_load(config_path, &config, error, sizeof(error)) != 0);

    {
        FILE *fp = fopen(config_path, "w");
        assert(fp != NULL);
        fprintf(fp, "PORT=18080\nROOT_DIR=%s\nMAX_CONNECTIONS=0\n", dir);
        fclose(fp);
        assert(lume_config_load(config_path, &config, error, sizeof(error)) != 0);
    }

    unlink(config_path);
    rmdir(dir);
    return 0;
}
