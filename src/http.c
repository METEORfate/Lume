#include "lume/http.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int find_request_line_end(const char *data, size_t length, size_t *line_end)
{
    size_t i;

    for (i = 0; i < length; i++) {
        if (data[i] == '\n') {
            *line_end = i;
            return 1;
        }
    }

    return 0;
}

lume_http_parse_result lume_http_parse_request(const char *data,
                                               size_t length,
                                               lume_http_request *request)
{
    size_t line_end;
    size_t line_length;
    char line[4096];
    char *save = NULL;
    char *method;
    char *uri;
    char *version;
    char *extra;

    if (!data || !request) {
        return LUME_HTTP_PARSE_BAD_REQUEST;
    }

    if (!find_request_line_end(data, length, &line_end)) {
        return LUME_HTTP_PARSE_INCOMPLETE;
    }

    line_length = line_end;
    if (line_length > 0 && data[line_length - 1] == '\r') {
        line_length--;
    }

    if (line_length == 0 || line_length >= sizeof(line)) {
        return LUME_HTTP_PARSE_BAD_REQUEST;
    }

    memcpy(line, data, line_length);
    line[line_length] = '\0';

    method = strtok_r(line, " \t", &save);
    uri = strtok_r(NULL, " \t", &save);
    version = strtok_r(NULL, " \t", &save);
    extra = strtok_r(NULL, " \t", &save);

    if (!method || !uri || !version || extra) {
        return LUME_HTTP_PARSE_BAD_REQUEST;
    }

    if (uri[0] != '/' || strlen(uri) >= sizeof(request->uri) || strlen(version) >= sizeof(request->version)) {
        return LUME_HTTP_PARSE_BAD_REQUEST;
    }

    if (strcmp(version, "HTTP/1.0") != 0 && strcmp(version, "HTTP/1.1") != 0) {
        return LUME_HTTP_PARSE_BAD_REQUEST;
    }

    request->method = strcmp(method, "GET") == 0 ? LUME_HTTP_METHOD_GET : LUME_HTTP_METHOD_UNSUPPORTED;
    snprintf(request->uri, sizeof(request->uri), "%s", uri);
    snprintf(request->version, sizeof(request->version), "%s", version);
    return LUME_HTTP_PARSE_OK;
}

const char *lume_http_status_reason(lume_http_status status)
{
    switch (status) {
    case LUME_HTTP_OK:
        return "OK";
    case LUME_HTTP_BAD_REQUEST:
        return "Bad Request";
    case LUME_HTTP_NOT_FOUND:
        return "Not Found";
    case LUME_HTTP_NOT_IMPLEMENTED:
        return "Not Implemented";
    case LUME_HTTP_INTERNAL_SERVER_ERROR:
        return "Internal Server Error";
    default:
        return "Unknown";
    }
}

int lume_http_append_response_headers(lume_buffer *buffer,
                                      lume_http_status status,
                                      const char *content_type,
                                      uintmax_t content_length)
{
    if (!content_type) {
        content_type = "application/octet-stream";
    }

    return lume_buffer_append_format(buffer,
                                     "HTTP/1.1 %d %s\r\n"
                                     "Server: Lume-C\r\n"
                                     "Content-Length: %" PRIuMAX "\r\n"
                                     "Content-Type: %s\r\n"
                                     "Connection: close\r\n"
                                     "\r\n",
                                     (int)status,
                                     lume_http_status_reason(status),
                                     content_length,
                                     content_type);
}

int lume_http_append_error_response(lume_buffer *buffer, lume_http_status status)
{
    char body[512];
    int body_length;

    body_length = snprintf(body,
                           sizeof(body),
                           "<!doctype html>\n"
                           "<html><head><meta charset=\"utf-8\"><title>%d %s</title></head>"
                           "<body><h1>%d %s</h1></body></html>\n",
                           (int)status,
                           lume_http_status_reason(status),
                           (int)status,
                           lume_http_status_reason(status));

    if (body_length < 0 || (size_t)body_length >= sizeof(body)) {
        return -1;
    }

    if (lume_http_append_response_headers(buffer,
                                          status,
                                          "text/html; charset=utf-8",
                                          (uintmax_t)body_length) != 0) {
        return -1;
    }

    return lume_buffer_append(buffer, body, (size_t)body_length);
}
