#include "lume/static_file.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fputs(content, fp);
    fclose(fp);
}

int main(void)
{
    char dir_template[] = "/tmp/lume_static_test_XXXXXX";
    char *dir = mkdtemp(dir_template);
    char index_path[PATH_MAX];
    char symlink_path[PATH_MAX];
    lume_static_file file;

    assert(dir != NULL);
    snprintf(index_path, sizeof(index_path), "%s/index.html", dir);
    snprintf(symlink_path, sizeof(symlink_path), "%s/passwd-link", dir);
    write_file(index_path, "hello");

    assert(lume_static_file_open(dir, "/", &file) == LUME_STATIC_OK);
    assert(file.fd >= 0);
    assert(file.size == 5);
    assert(strcmp(file.content_type, "text/html; charset=utf-8") == 0);
    lume_static_file_close(&file);

    assert(lume_static_file_open(dir, "/missing.html", &file) == LUME_STATIC_NOT_FOUND);
    assert(lume_static_file_open(dir, "/../etc/passwd", &file) == LUME_STATIC_BAD_REQUEST);
    assert(lume_static_file_open(dir, "/%2e%2e/etc/passwd", &file) == LUME_STATIC_BAD_REQUEST);
    assert(lume_static_file_open(dir, "/bad%zzpath", &file) == LUME_STATIC_BAD_REQUEST);

    if (symlink("/etc/passwd", symlink_path) == 0) {
        assert(lume_static_file_open(dir, "/passwd-link", &file) == LUME_STATIC_BAD_REQUEST);
        unlink(symlink_path);
    }

    unlink(index_path);
    rmdir(dir);
    return 0;
}
