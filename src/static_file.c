#include "lume/static_file.h"

#include "lume/mime.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void lume_static_file_init(lume_static_file *file)
{
    if (!file) {
        return;
    }

    file->fd = -1;
    file->size = 0;
    file->path[0] = '\0';
    file->content_type = "application/octet-stream";
}

void lume_static_file_close(lume_static_file *file)
{
    if (!file) {
        return;
    }

    if (file->fd >= 0) {
        close(file->fd);
    }
    lume_static_file_init(file);
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static int decode_uri_path(const char *uri, char *decoded, size_t decoded_size)
{
    size_t i = 0;
    size_t out = 0;

    if (!uri || uri[0] != '/' || decoded_size == 0) {
        return -1;
    }

    while (uri[i] && uri[i] != '?' && uri[i] != '#') {
        unsigned char ch;

        if (out + 1 >= decoded_size) {
            return -1;
        }

        if (uri[i] == '%') {
            int high;
            int low;

            if (!uri[i + 1] || !uri[i + 2]) {
                return -1;
            }

            high = hex_value(uri[i + 1]);
            low = hex_value(uri[i + 2]);
            if (high < 0 || low < 0) {
                return -1;
            }

            ch = (unsigned char)((high << 4) | low);
            i += 3;
        } else {
            ch = (unsigned char)uri[i++];
        }

        if (ch == '\0' || iscntrl(ch) || ch == '\\') {
            return -1;
        }

        decoded[out++] = (char)ch;
    }

    decoded[out] = '\0';
    return 0;
}

static int append_segment(char *relative, size_t relative_size, const char *segment)
{
    size_t current = strlen(relative);
    size_t segment_len = strlen(segment);
    size_t need_separator = current > 0 ? 1 : 0;

    if (current + need_separator + segment_len + 1 > relative_size) {
        return -1;
    }

    if (need_separator) {
        relative[current++] = '/';
        relative[current] = '\0';
    }

    memcpy(relative + current, segment, segment_len + 1);
    return 0;
}

static int normalize_decoded_path(const char *decoded, char *relative, size_t relative_size)
{
    char copy[PATH_MAX];
    char *save = NULL;
    char *segment;
    size_t decoded_len;
    int wants_index;

    if (!decoded || decoded[0] != '/' || strlen(decoded) >= sizeof(copy)) {
        return -1;
    }

    decoded_len = strlen(decoded);
    wants_index = decoded_len == 1 || decoded[decoded_len - 1] == '/';

    snprintf(copy, sizeof(copy), "%s", decoded);
    relative[0] = '\0';

    segment = strtok_r(copy, "/", &save);
    while (segment) {
        if (strcmp(segment, "..") == 0) {
            return -1;
        }

        if (strcmp(segment, ".") != 0 && *segment != '\0') {
            if (append_segment(relative, relative_size, segment) != 0) {
                return -1;
            }
        }

        segment = strtok_r(NULL, "/", &save);
    }

    if (relative[0] == '\0' || wants_index) {
        if (append_segment(relative, relative_size, "index.html") != 0) {
            return -1;
        }
    }

    return 0;
}

static int path_is_inside_root(const char *root, const char *path)
{
    size_t root_len = strlen(root);

    if (strcmp(root, "/") == 0) {
        return 1;
    }

    return strncmp(root, path, root_len) == 0 &&
           (path[root_len] == '\0' || path[root_len] == '/');
}

lume_static_result lume_static_file_open(const char *root_dir,
                                         const char *uri,
                                         lume_static_file *file)
{
    char decoded[PATH_MAX];
    char relative[PATH_MAX];
    char joined[PATH_MAX];
    char resolved[PATH_MAX];
    struct stat st;
    int fd;
    int written;

    if (!root_dir || !uri || !file) {
        return LUME_STATIC_BAD_REQUEST;
    }

    lume_static_file_init(file);

    if (decode_uri_path(uri, decoded, sizeof(decoded)) != 0 ||
        normalize_decoded_path(decoded, relative, sizeof(relative)) != 0) {
        return LUME_STATIC_BAD_REQUEST;
    }

    written = snprintf(joined, sizeof(joined), "%s/%s", root_dir, relative);
    if (written < 0 || (size_t)written >= sizeof(joined)) {
        return LUME_STATIC_BAD_REQUEST;
    }

    if (!realpath(joined, resolved)) {
        if (errno == ENOENT || errno == ENOTDIR || errno == EACCES) {
            return LUME_STATIC_NOT_FOUND;
        }
        return LUME_STATIC_NOT_FOUND;
    }

    if (!path_is_inside_root(root_dir, resolved)) {
        return LUME_STATIC_BAD_REQUEST;
    }

    if (stat(resolved, &st) != 0 || !S_ISREG(st.st_mode)) {
        return LUME_STATIC_NOT_FOUND;
    }

    fd = open(resolved, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return LUME_STATIC_NOT_FOUND;
    }

    file->fd = fd;
    file->size = st.st_size;
    snprintf(file->path, sizeof(file->path), "%s", resolved);
    file->content_type = lume_mime_type_for_path(resolved);
    return LUME_STATIC_OK;
}
