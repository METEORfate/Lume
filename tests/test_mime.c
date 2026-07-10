#include "lume/mime.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    assert(strcmp(lume_mime_type_for_path("index.html"), "text/html; charset=utf-8") == 0);
    assert(strcmp(lume_mime_type_for_path("style.CSS"), "text/css; charset=utf-8") == 0);
    assert(strcmp(lume_mime_type_for_path("image.jpeg"), "image/jpeg") == 0);
    assert(strcmp(lume_mime_type_for_path("data.unknown"), "application/octet-stream") == 0);
    assert(strcmp(lume_mime_type_for_path("no-extension"), "application/octet-stream") == 0);
    return 0;
}
