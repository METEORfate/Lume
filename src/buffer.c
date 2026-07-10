#include "lume/buffer.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void lume_buffer_init(lume_buffer *buffer)
{
    if (!buffer) {
        return;
    }

    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
    buffer->offset = 0;
}

void lume_buffer_free(lume_buffer *buffer)
{
    if (!buffer) {
        return;
    }

    free(buffer->data);
    lume_buffer_init(buffer);
}

void lume_buffer_clear(lume_buffer *buffer)
{
    if (!buffer) {
        return;
    }

    buffer->length = 0;
    buffer->offset = 0;
    if (buffer->data && buffer->capacity > 0) {
        buffer->data[0] = '\0';
    }
}

int lume_buffer_reserve(lume_buffer *buffer, size_t capacity)
{
    size_t new_capacity;
    char *new_data;

    if (!buffer) {
        return -1;
    }

    if (capacity <= buffer->capacity) {
        return 0;
    }

    new_capacity = buffer->capacity ? buffer->capacity : 1024;
    while (new_capacity < capacity) {
        if (new_capacity > SIZE_MAX / 2) {
            return -1;
        }
        new_capacity *= 2;
    }

    new_data = realloc(buffer->data, new_capacity);
    if (!new_data) {
        return -1;
    }

    buffer->data = new_data;
    buffer->capacity = new_capacity;
    return 0;
}

int lume_buffer_append(lume_buffer *buffer, const void *data, size_t length)
{
    if (!buffer || (!data && length > 0)) {
        return -1;
    }

    if (length == 0) {
        return 0;
    }

    if (buffer->length > SIZE_MAX - length - 1) {
        return -1;
    }

    if (lume_buffer_reserve(buffer, buffer->length + length + 1) != 0) {
        return -1;
    }

    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return 0;
}

int lume_buffer_append_str(lume_buffer *buffer, const char *text)
{
    if (!text) {
        return -1;
    }

    return lume_buffer_append(buffer, text, strlen(text));
}

int lume_buffer_append_format(lume_buffer *buffer, const char *format, ...)
{
    va_list args;
    va_list copy;
    int needed;
    int written;

    if (!buffer || !format) {
        return -1;
    }

    va_start(args, format);
    va_copy(copy, args);
    needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);

    if (needed < 0) {
        va_end(args);
        return -1;
    }

    if (buffer->length > SIZE_MAX - (size_t)needed - 1) {
        va_end(args);
        return -1;
    }

    if (lume_buffer_reserve(buffer, buffer->length + (size_t)needed + 1) != 0) {
        va_end(args);
        return -1;
    }

    written = vsnprintf(buffer->data + buffer->length,
                        buffer->capacity - buffer->length,
                        format,
                        args);
    va_end(args);

    if (written != needed) {
        return -1;
    }

    buffer->length += (size_t)written;
    return 0;
}

size_t lume_buffer_remaining(const lume_buffer *buffer)
{
    if (!buffer || buffer->offset >= buffer->length) {
        return 0;
    }

    return buffer->length - buffer->offset;
}

const char *lume_buffer_current(const lume_buffer *buffer)
{
    if (!buffer || !buffer->data) {
        return "";
    }

    if (buffer->offset >= buffer->length) {
        return buffer->data + buffer->length;
    }

    return buffer->data + buffer->offset;
}

void lume_buffer_advance(lume_buffer *buffer, size_t amount)
{
    size_t remaining;

    if (!buffer) {
        return;
    }

    remaining = lume_buffer_remaining(buffer);
    if (amount >= remaining) {
        buffer->offset = buffer->length;
    } else {
        buffer->offset += amount;
    }
}
