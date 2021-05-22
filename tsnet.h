#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "hashtable.h"

typedef struct tsnet TSNET;

typedef int socket_t;
typedef int tsnet_event_t;
typedef void(*tsnet_cb_t)(TSNET *tsnet, socket_t client_fd, uint8_t *data, ssize_t data_len);

enum tsnet_server_type {
	TSNET_EPOLL,
	//TSNET_IO_URING, /* not yet */
};

enum tsnet_event_type {
	TSNET_EVENT_ACCEPT = 0, /* callback vector index */
	TSNET_EVENT_CLOSE,
	TSNET_EVENT_RECV,
	TSNET_EVENT_SEND_COMPLETE,
	TSNET_EVENT_MAX
};

enum tsnet_send_type {
	TSNET_SEND_MEMORY = 1,
	TSNET_SEND_FILE
};

struct tsnet_client {
	socket_t fd;
	char ip[16];
	uint16_t port;
};

struct tsnet_send_request {
	socket_t fd;
	char send_type;
	uint8_t *send_data;
	size_t send_len, sended_len;
	int sendfile_fd;
};

typedef struct tsnet {
	char type;

	socket_t fd;
	int epfd;

	char ip[16];
	uint16_t port;
	int backlog;
	int max_client;

	tsnet_cb_t cb_vec[TSNET_EVENT_MAX];
	HashTable *send_request_client; // client that sent the send request 
	HashTable *connected_client;

	char is_bind;
} TSNET;

TSNET * tsnet_create(int type, int backlog, int max_client);
void tsnet_delete(TSNET *tsnet);

int tsnet_bind(TSNET *tsnet, const char *ip, uint16_t port);
int tsnet_addListener(TSNET *tsnet, tsnet_event_t event, tsnet_cb_t cb);
int tsnet_loop(TSNET *tsnet);

int tsnet_send(TSNET *tsnet, socket_t client_fd, const void *data, size_t data_len);
int tsnet_sendfile(TSNET *tsnet, socket_t client_fd, const char *file_path);
int tsnet_get_client_info(TSNET *tsnet, socket_t client_fd, struct tsnet_client *client);

const char *tsnet_get_last_error();
