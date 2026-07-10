#ifndef LUME_BUFFER_H
#define LUME_BUFFER_H

#include <stddef.h>

typedef struct lume_buffer {
    char *data;
    size_t length;
    size_t capacity;
    size_t offset;
} lume_buffer;

void lume_buffer_init(lume_buffer *buffer);
void lume_buffer_free(lume_buffer *buffer);
void lume_buffer_clear(lume_buffer *buffer);
int lume_buffer_reserve(lume_buffer *buffer, size_t capacity);
int lume_buffer_append(lume_buffer *buffer, const void *data, size_t length);
int lume_buffer_append_str(lume_buffer *buffer, const char *text);
int lume_buffer_append_format(lume_buffer *buffer, const char *format, ...);
size_t lume_buffer_remaining(const lume_buffer *buffer);
const char *lume_buffer_current(const lume_buffer *buffer);
void lume_buffer_advance(lume_buffer *buffer, size_t amount);

#endif
