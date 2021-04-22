#include "tsnet.h"

void accept_cb(socket_t client_fd, uint8_t *data, ssize_t data_len)
{
	struct tsnet_client client;

	if ( tsnet_get_client_info(client_fd, &client) < 0 ) {
		fprintf(stderr, "%s\n", tsnet_get_last_error());
		return;
	}

	printf("connected with %s:%d\n", client.ip, client.port);
}

void read_cb(socket_t client_fd, uint8_t *data, ssize_t data_len)
{
	if ( send(client_fd, data, data_len, 0) <= 0 ) {
		fprintf(stderr, "send() is failed: (errmsg: %s, errno: %d)\n", strerror(errno), errno);
	}
}

void close_cb(socket_t client_fd, uint8_t *data, ssize_t data_len)
{
	struct tsnet_client client;

	if ( tsnet_get_client_info(client_fd, &client) < 0 ) {
		fprintf(stderr, "%s\n", tsnet_get_last_error());
		return;
	}

	printf("disconnected with %s:%d\n", client.ip, client.port);
}

int main(int argc, char **argv)
{
	TSNET *tsnet = NULL;

	tsnet = tsnet_create(TSNET_EPOLL, 0, 0);
	if ( !tsnet ) {
		fprintf(stderr, "%s\n", tsnet_get_last_error());
		goto out;
	}

	if ( tsnet_setup(tsnet, "0.0.0.0", 8080) < 0 ) {
		fprintf(stderr, "%s\n", tsnet_get_last_error());
		goto out;
	}

	if ( tsnet_addListener(tsnet, TSNET_EVENT_ACCEPT, accept_cb) < 0 ) {
		fprintf(stderr, "%s\n", tsnet_get_last_error());
		goto out;
	}
	if ( tsnet_addListener(tsnet, TSNET_EVENT_READ, read_cb) < 0 ) {
		fprintf(stderr, "%s\n", tsnet_get_last_error());
		goto out;
	}
	if ( tsnet_addListener(tsnet, TSNET_EVENT_CLOSE, close_cb) < 0 ) {
		fprintf(stderr, "%s\n", tsnet_get_last_error());
		goto out;
	}

	if ( tsnet_loop(tsnet) < 0 ) {
		fprintf(stderr, "%s\n", tsnet_get_last_error());
		goto out;
	}

	tsnet_delete(tsnet);

	return 0;

out:
	tsnet_delete(tsnet);

	return 1;
}
