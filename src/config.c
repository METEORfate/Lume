#include "lume/config.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void set_error(char *error, size_t error_len, const char *format, ...)
{
    va_list args;

    if (!error || error_len == 0) {
        return;
    }

    va_start(args, format);
    vsnprintf(error, error_len, format, args);
    va_end(args);
}

static char *trim(char *text)
{
    char *end;

    while (*text && isspace((unsigned char)*text)) {
        text++;
    }

    if (*text == '\0') {
        return text;
    }

    end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return text;
}

void lume_config_init(lume_config *config)
{
    if (!config) {
        return;
    }

    config->port = 8080;
    config->root_dir[0] = '\0';
}

static int parse_port(const char *value, int *port)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value) {
        return -1;
    }

    end = trim(end);
    if (*end != '\0' || parsed < 1 || parsed > 65535) {
        return -1;
    }

    *port = (int)parsed;
    return 0;
}

int lume_config_load(const char *path, lume_config *config, char *error, size_t error_len)
{
    FILE *fp;
    char line[1024];
    char root_value[PATH_MAX];
    char resolved_root[PATH_MAX];
    int line_no = 0;
    lume_config loaded;
    struct stat st;

    if (!path || !config) {
        set_error(error, error_len, "invalid config arguments");
        return -1;
    }

    fp = fopen(path, "r");
    if (!fp) {
        set_error(error, error_len, "failed to open config '%s': %s", path, strerror(errno));
        return -1;
    }

    lume_config_init(&loaded);
    root_value[0] = '\0';

    while (fgets(line, sizeof(line), fp)) {
        char *comment;
        char *equals;
        char *key;
        char *value;

        line_no++;
        comment = strchr(line, '#');
        if (comment) {
            *comment = '\0';
        }

        key = trim(line);
        if (*key == '\0') {
            continue;
        }

        equals = strchr(key, '=');
        if (!equals) {
            fclose(fp);
            set_error(error, error_len, "config line %d is missing '='", line_no);
            return -1;
        }

        *equals = '\0';
        value = trim(equals + 1);
        key = trim(key);

        if (strcmp(key, "PORT") == 0) {
            if (parse_port(value, &loaded.port) != 0) {
                fclose(fp);
                set_error(error, error_len, "config line %d has invalid PORT", line_no);
                return -1;
            }
        } else if (strcmp(key, "ROOT_DIR") == 0) {
            if (*value == '\0' || strlen(value) >= sizeof(root_value)) {
                fclose(fp);
                set_error(error, error_len, "config line %d has invalid ROOT_DIR", line_no);
                return -1;
            }
            snprintf(root_value, sizeof(root_value), "%s", value);
        } else {
            fclose(fp);
            set_error(error, error_len, "config line %d has unknown key '%s'", line_no, key);
            return -1;
        }
    }

    if (ferror(fp)) {
        fclose(fp);
        set_error(error, error_len, "failed to read config '%s'", path);
        return -1;
    }

    fclose(fp);

    if (root_value[0] == '\0') {
        set_error(error, error_len, "ROOT_DIR is required");
        return -1;
    }

    if (!realpath(root_value, resolved_root)) {
        set_error(error, error_len, "failed to resolve ROOT_DIR '%s': %s", root_value, strerror(errno));
        return -1;
    }

    if (stat(resolved_root, &st) != 0 || !S_ISDIR(st.st_mode)) {
        set_error(error, error_len, "ROOT_DIR '%s' is not a directory", resolved_root);
        return -1;
    }

    snprintf(loaded.root_dir, sizeof(loaded.root_dir), "%s", resolved_root);
    *config = loaded;
    return 0;
}
