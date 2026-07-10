#include "lume/http.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    lume_http_request request;
    lume_buffer buffer;
    const char *get_request = "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n";
    const char *post_request = "POST / HTTP/1.1\r\n\r\n";
    const char *bad_request = "GET HTTP/1.1\r\n\r\n";
    const char *bad_version = "GET / HTTP/2\r\n\r\n";
    const char *incomplete = "GET / HTTP/1.1";

    assert(lume_http_parse_request(get_request, strlen(get_request), &request) == LUME_HTTP_PARSE_OK);
    assert(request.method == LUME_HTTP_METHOD_GET);
    assert(strcmp(request.uri, "/index.html") == 0);
    assert(strcmp(request.version, "HTTP/1.1") == 0);

    assert(lume_http_parse_request(post_request, strlen(post_request), &request) == LUME_HTTP_PARSE_OK);
    assert(request.method == LUME_HTTP_METHOD_UNSUPPORTED);

    assert(lume_http_parse_request(bad_request, strlen(bad_request), &request) == LUME_HTTP_PARSE_BAD_REQUEST);
    assert(lume_http_parse_request(bad_version, strlen(bad_version), &request) == LUME_HTTP_PARSE_BAD_REQUEST);
    assert(lume_http_parse_request(incomplete, strlen(incomplete), &request) == LUME_HTTP_PARSE_INCOMPLETE);

    lume_buffer_init(&buffer);
    assert(lume_http_append_error_response(&buffer, LUME_HTTP_NOT_FOUND) == 0);
    assert(strstr(buffer.data, "HTTP/1.1 404 Not Found") != NULL);
    assert(strstr(buffer.data, "Content-Length:") != NULL);
    lume_buffer_free(&buffer);

    return 0;
}
