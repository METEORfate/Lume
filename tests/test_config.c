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
    assert(strcmp(config.root_dir, dir) == 0);

    write_config(config_path, dir, "70000");
    assert(lume_config_load(config_path, &config, error, sizeof(error)) != 0);

    unlink(config_path);
    rmdir(dir);
    return 0;
}
