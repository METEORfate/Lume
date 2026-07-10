#ifndef LUME_HTTP_H
#define LUME_HTTP_H

#include <stddef.h>
#include <stdint.h>

#include "lume/buffer.h"

#define LUME_HTTP_MAX_URI 2048

typedef enum lume_http_status {
    LUME_HTTP_OK = 200,
    LUME_HTTP_BAD_REQUEST = 400,
    LUME_HTTP_NOT_FOUND = 404,
    LUME_HTTP_NOT_IMPLEMENTED = 501,
    LUME_HTTP_INTERNAL_SERVER_ERROR = 500
} lume_http_status;

typedef enum lume_http_method {
    LUME_HTTP_METHOD_GET,
    LUME_HTTP_METHOD_UNSUPPORTED
} lume_http_method;

typedef enum lume_http_parse_result {
    LUME_HTTP_PARSE_OK,
    LUME_HTTP_PARSE_INCOMPLETE,
    LUME_HTTP_PARSE_BAD_REQUEST
} lume_http_parse_result;

typedef struct lume_http_request {
    lume_http_method method;
    char uri[LUME_HTTP_MAX_URI];
    char version[16];
} lume_http_request;

lume_http_parse_result lume_http_parse_request(const char *data, size_t length, lume_http_request *request);
const char *lume_http_status_reason(lume_http_status status);
int lume_http_append_response_headers(lume_buffer *buffer,
                                      lume_http_status status,
                                      const char *content_type,
                                      uintmax_t content_length);
int lume_http_append_error_response(lume_buffer *buffer, lume_http_status status);

#endif
