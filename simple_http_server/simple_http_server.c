#include <time.h>
#include <sys/stat.h>

#include "simple_http_server.h"
#include "sha256.h"

/* Not seriously written server code. Please refer to the example level only. */

void bin2str(const unsigned char *src, size_t src_len, char *dst, size_t dst_len)
{
	unsigned char uTable[256] = {0};

	uTable[0x00] = '0'; uTable[0x01] = '1'; uTable[0x02] = '2'; uTable[0x03] = '3';
	uTable[0x04] = '4'; uTable[0x05] = '5'; uTable[0x06] = '6'; uTable[0x07] = '7';
	uTable[0x08] = '8'; uTable[0x09] = '9'; uTable[0x0a] = 'a'; uTable[0x0b] = 'b';
	uTable[0x0c] = 'c'; uTable[0x0d] = 'd'; uTable[0x0e] = 'e'; uTable[0x0f] = 'f';

	uTable[0x10] = '1'; uTable[0x20] = '2'; uTable[0x30] = '3'; uTable[0x40] = '4';
	uTable[0x50] = '5'; uTable[0x60] = '6'; uTable[0x70] = '7'; uTable[0x80] = '8';
	uTable[0x90] = '9'; uTable[0xa0] = 'a'; uTable[0xb0] = 'b'; uTable[0xc0] = 'c';
	uTable[0xd0] = 'd'; uTable[0xe0] = 'e'; uTable[0xf0] = 'f';

	for ( size_t i = 0, j = 0; i < src_len; i++ ) {
		dst[j++] = uTable[src[i] & 0xf0];
		dst[j++] = uTable[src[i] & 0x0f];
	}
}

static void make_http_response(int code, const char *msg, struct http_response *response)
{
	memset(response, 0x00, sizeof(struct http_response));
	response->buf_len += snprintf(response->buf, sizeof(response->buf), "HTTP/1.1 %d %s\r\n", code, msg);
}

static void add_http_response_header(const char *name, const char *value, struct http_response *response, char last)
{
	if ( last ) response->buf_len += snprintf(response->buf + response->buf_len, sizeof(response->buf) - response->buf_len, "%s: %s\r\n\r\n", name, value);
	else response->buf_len += snprintf(response->buf + response->buf_len, sizeof(response->buf) - response->buf_len, "%s: %s\r\n", name, value);
}

const char * make_etag(const char *path)
{
	size_t nread;
	uint8_t hash[32];
	uint8_t buf[BUFSIZ];
	static char etag[64] = {0};
	FILE *fp = NULL;
	SHA256_CTX ctx;

	memset(etag, 0x00, sizeof(etag));

	sha256_init(&ctx);
	
	if ( !(fp = fopen(path, "rb")) ) {
		fprintf(stderr, "fopen() is failed: (errmsg: %s, errno: %d, path: %s)\n", strerror(errno), errno, path);
		goto out;
	}

	while ( (nread = fread(buf, 1, sizeof(buf), fp)) > 0 ) {
		sha256_update(&ctx, buf, nread);
	}
	sha256_final(&ctx, hash);
	bin2str(hash, sizeof(hash), etag, sizeof(etag));

out:
	if ( fp ) fclose(fp);

	return etag;
}

static int send_http_response(TSNET *tsnet, socket_t client_fd, int code, const char *msg, const char *path, const char *type)
{
	time_t t = time(NULL);
	char date[64];
	char number[16];
	struct stat st;
	struct http_response response;

	if ( stat(path, &st) < 0 ) {
		fprintf(stderr, "stat() is failed: (errmsg: %s, errno: %d, path: %s)\n", strerror(errno), errno, path);
		goto out;
	}

	make_http_response(code, msg, &response);
	memset(number, 0x00, sizeof(number));
	snprintf(number, sizeof(number), "%ld", st.st_size);
	add_http_response_header("Content-Length", number, &response, 0);
	add_http_response_header("Content-Type", "text/html", &response, 0);
	strftime(date, sizeof(date), "%c", localtime(&st.st_mtime));
	add_http_response_header("Last-Modified", date, &response, 0);
	add_http_response_header("Accept-Ranges", "bytes", &response, 0);
	add_http_response_header("ETag", make_etag(path), &response, 0);
	strftime(date, sizeof(date), "%c", localtime(&t));
	add_http_response_header("Date", date, &response, 1);
	
	if ( tsnet_send(tsnet, client_fd, response.buf, response.buf_len) < 0 ) {
		fprintf(stderr, "%s", tsnet_get_last_error());
		goto out;
	}
	
	if ( tsnet_sendfile(tsnet, client_fd, path) < 0 ) {
		fprintf(stderr, "%s", tsnet_get_last_error());
		goto out;
	}

	//printf("---------------------------\n");
	//printf("%s", response.buf);

	return 0;

out:
	return -1;
}

void accept_cb(TSNET *tsnet, socket_t client_fd, uint8_t *data, ssize_t data_len)
{
	struct tsnet_client client;

	if ( tsnet_get_client_info(tsnet, client_fd, &client) < 0 ) {
		fprintf(stderr, "%s\n", tsnet_get_last_error());
		return;
	}

	printf("connected with (%d:%s:%d)\n", client_fd, client.ip, client.port);
}

void recv_cb(TSNET *tsnet, socket_t client_fd, uint8_t *data, ssize_t data_len)
{
	size_t prevbuflen = 0;
	struct http_request http_request;
	struct http_request *http_request_p;
	HashTableBucket *bucket;
	HashTable *http_request_table = tsnet->user_data;

	bucket = http_request_table->find(http_request_table, &client_fd, sizeof(client_fd));
	if ( bucket ) {
		http_request_p = bucket->value;
		char *p = http_request_p->buf + http_request_p->received_len;

		prevbuflen = http_request_p->received_len;
		http_request_p->received_len += data_len;

		memcpy(p, data, data_len);
	} else {
		memset(&http_request, 0x00, sizeof(http_request));
		
		http_request.received_len += data_len;
		http_request.num_headers = sizeof(http_request.headers) / sizeof(http_request.headers[0]);
		
		memcpy(http_request.buf, data, data_len);
		
		http_request_p = &http_request;
	}
		
	// parse http headers
	if ( phr_parse_request(http_request_p->buf, http_request_p->received_len, &http_request_p->method, &http_request_p->method_len, &http_request_p->path, &http_request_p->path_len, &http_request_p->minor_version, http_request_p->headers, &http_request_p->num_headers, prevbuflen) > 0 ) {
		char method[16] = {0};
		char path[512] = {0};
			
		memcpy(method, http_request_p->method, http_request_p->method_len);
		if ( http_request_p->path_len == 1 /* probably '/' */ ) {
			snprintf(path, sizeof(path), "./index.html");
		} else {
			path[0] = '.';
			memcpy(path + 1, http_request_p->path, http_request_p->path_len);
		}
		//printf("method: %s\n", method);
		//printf("path:   %s\n", path);

		for ( size_t i = 0; i < http_request_p->num_headers; i++ ) {
			char name[64] = {0};
			char value[256] = {0};

			memcpy(name, http_request_p->headers[i].name, http_request_p->headers[i].name_len);
			memcpy(value, http_request_p->headers[i].value, http_request_p->headers[i].value_len);
			//printf("name:   %s\n", name);
			//printf("value:  %s\n", value);
		}

		//TODO: image/gif, image/jpeg, image/png, application/octet-stream
		(void)send_http_response(tsnet, client_fd, 200, "OK", path, "text/html");
	}
}

void close_cb(TSNET *tsnet, socket_t client_fd, uint8_t *data, ssize_t data_len)
{
	struct tsnet_client client;

	if ( tsnet_get_client_info(tsnet, client_fd, &client) < 0 ) {
		fprintf(stderr, "%s\n", tsnet_get_last_error());
		return;
	}
	
	printf("disconnected with (%d:%s:%d)\n", client_fd, client.ip, client.port);
}

int main(int argc, char **argv)
{
	TSNET *tsnet = NULL;
	HashTable *http_request_table = NULL;

	if ( argc != 2 ) {
		fprintf(stderr, "%s (port)\n", argv[0]);
		return 1;
	}

	tsnet = tsnet_create(TSNET_EPOLL, 0, 0);
	if ( !tsnet ) {
		fprintf(stderr, "%s\n", tsnet_get_last_error());
		goto out;
	}
	
	if ( !(http_request_table = ht_create(0, 0, 0)) ) {
		fprintf(stderr, "%s\n", tsnet_get_last_error());
		goto out;
	}

	tsnet_set_user_data(tsnet, http_request_table);

	if ( tsnet_bind(tsnet, "0.0.0.0", atoi(argv[1])) < 0 ) {
		fprintf(stderr, "%s\n", tsnet_get_last_error());
		goto out;
	}

	(void)tsnet_addListener(tsnet, TSNET_EVENT_ACCEPT, accept_cb);
	(void)tsnet_addListener(tsnet, TSNET_EVENT_RECV, recv_cb);
	(void)tsnet_addListener(tsnet, TSNET_EVENT_CLOSE, close_cb);

	if ( tsnet_loop(tsnet) < 0 ) {
		fprintf(stderr, "%s\n", tsnet_get_last_error());
		goto out;
	}

	ht_delete(http_request_table);
	tsnet_delete(tsnet);

	return 0;

out:
	ht_delete(http_request_table);
	tsnet_delete(tsnet);

	return 1;
}
