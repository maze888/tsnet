#pragma once

/* Not seriously written server code. Please refer to the example level only. */

#include "tsnet.h"
#include "picohttpparser.h"

#define MAX_HTTP_HEADER 64

struct http_request {
	char buf[65535];
	size_t received_len;
	
	const char *method, *path;
	size_t method_len, path_len;
	int minor_version;

	struct phr_header headers[MAX_HTTP_HEADER];
	size_t num_headers;
};

#if 0
HTTP/1.1 200 OK\r\n
Content-Length: 55\r\n
Content-Type: text/html\r\n
Last-Modified: Wed, 12 Aug 1998 15:03:50 GMT\r\n
Accept-Ranges: bytes\r\n
ETag: “04f97692cbd1:377”\r\n
Date: Thu, 19 Jun 2008 19:29:07 GMT\r\n
\r\n
#endif

struct http_response {
	char buf[65535];
	size_t buf_len;
};
