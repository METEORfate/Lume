#include "lume/mime.h"

#include <string.h>
#include <strings.h>

typedef struct mime_entry {
    const char *extension;
    const char *type;
} mime_entry;

static const mime_entry MIME_TYPES[] = {
    {".html", "text/html; charset=utf-8"},
    {".htm", "text/html; charset=utf-8"},
    {".css", "text/css; charset=utf-8"},
    {".js", "application/javascript; charset=utf-8"},
    {".mjs", "application/javascript; charset=utf-8"},
    {".json", "application/json; charset=utf-8"},
    {".txt", "text/plain; charset=utf-8"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".svg", "image/svg+xml"},
    {".ico", "image/x-icon"},
    {".webp", "image/webp"},
    {".wasm", "application/wasm"},
};

const char *lume_mime_type_for_path(const char *path)
{
    const char *extension;
    size_t i;

    if (!path) {
        return "application/octet-stream";
    }

    extension = strrchr(path, '.');
    if (!extension) {
        return "application/octet-stream";
    }

    for (i = 0; i < sizeof(MIME_TYPES) / sizeof(MIME_TYPES[0]); i++) {
        if (strcasecmp(extension, MIME_TYPES[i].extension) == 0) {
            return MIME_TYPES[i].type;
        }
    }

    return "application/octet-stream";
}
