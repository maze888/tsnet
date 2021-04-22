#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/socket.h>

typedef int socket_t;
typedef int tsnet_event_t;
typedef void(*tsnet_cb_t)(socket_t client_fd, uint8_t *data, ssize_t data_len);

enum {
	TSNET_EPOLL,
	//TSNET_IO_URING, /* not yet */
};

enum 
{
	TSNET_EVENT_ACCEPT = 0, /* callback vector index */
	TSNET_EVENT_CLOSE,
	TSNET_EVENT_READ,
};

struct tsnet_client {
	socket_t fd;
	char ip[16];
	uint16_t port;
};

typedef struct {
	char type;

	socket_t fd;
	int epfd;

	char ip[16];
	uint16_t port;
	int backlog;
	int max_client;

	tsnet_cb_t cb_vec[TSNET_EVENT_READ + 1]; /* is last event */

	char is_setup;
} TSNET;

TSNET * tsnet_create(int type, int backlog, int max_client);
void tsnet_delete(TSNET *tsnet);

int tsnet_setup(TSNET *tsnet, const char *ip, uint16_t port);
int tsnet_addListener(TSNET *tsnet, tsnet_event_t event, tsnet_cb_t cb);
int tsnet_loop(TSNET *tsnet);

int tsnet_get_client_info(socket_t client_fd, struct tsnet_client *client);

const char *tsnet_get_last_error();
