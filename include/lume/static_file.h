#ifndef LUME_STATIC_FILE_H
#define LUME_STATIC_FILE_H

#include <limits.h>
#include <sys/types.h>

typedef enum lume_static_result {
    LUME_STATIC_OK,
    LUME_STATIC_BAD_REQUEST,
    LUME_STATIC_NOT_FOUND
} lume_static_result;

typedef struct lume_static_file {
    int fd;
    off_t size;
    char path[PATH_MAX];
    const char *content_type;
} lume_static_file;

void lume_static_file_init(lume_static_file *file);
void lume_static_file_close(lume_static_file *file);
lume_static_result lume_static_file_open(const char *root_dir,const char *uri,lume_static_file *file);
#endif
