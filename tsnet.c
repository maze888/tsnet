#include "tsnet.h"
#include "tsnet_common_inter.h"
#include "tsnet_epoll.h"

static void send_request_erase_free(void *data)
{
	struct tsnet_send_request *srq;

	if ( data ) {
		srq = data;
		if ( srq->send_type == TSNET_SEND_MEMORY ) {
			safe_free(srq->send_data);
		}
		else if ( srq->send_type == TSNET_SEND_FILE ) {
			safe_close(srq->sendfile_fd);
		}
	}
}

static int close_client(TSNET *tsnet, int client_fd)
{
	if ( epoll_event_control(tsnet->epfd, EPOLL_CTL_DEL, client_fd, 0) < 0 ) {
		TSNET_SET_ERROR("epoll_ctl(EPOLL_CTL_DEL) is failed: (errmsg: %s, errno: %d)", strerror(errno), errno);
		goto out;
	}

	if ( tsnet->cb_vec[TSNET_EVENT_CLOSE] ) tsnet->cb_vec[TSNET_EVENT_CLOSE](tsnet, client_fd, NULL, 0);
	
	(void)tsnet->send_request_client->erase(tsnet->send_request_client, &client_fd, sizeof(client_fd), 1);
	if ( tsnet->connected_client->erase(tsnet->connected_client, &client_fd, sizeof(client_fd), 0) < 0 ) goto out;
	
	safe_close(client_fd);
						
	return 0;

out:
	return -1;
}

static int send_data_to_client(TSNET *tsnet, HashTableBucket *bucket, int client_fd /* same srq->fd */)
{
	ssize_t nsend;
	char send_done = 0;
	struct tsnet_send_request *srq;

	srq = bucket->value;
	
	if ( srq->send_type == TSNET_SEND_MEMORY ) {
		do {
			nsend = send(srq->fd, srq->send_data + srq->sended_len, srq->send_len - srq->sended_len, 0);
			//printf("send nsend:     %ld\n", nsend);
			if ( nsend > 0 ) srq->sended_len += nsend;
		} while ( nsend > 0 );
	}
	else if ( srq->send_type == TSNET_SEND_FILE ) {
		do {
			nsend = sendfile(client_fd, srq->sendfile_fd /* offset auto move */, NULL, srq->send_len - srq->sended_len);
			//printf("sendfile nsend: %ld\n", nsend);
			if ( nsend > 0 ) srq->sended_len += nsend;
		} while ( nsend > 0 );
	}
	else { // it never happens, but i put in the code just in case 
		TSNET_SET_ERROR("invalid send type (type: %d)", srq->send_type);
		goto out;
	}
	
	if ( nsend < 0 ) {
		if ( errno != EWOULDBLOCK ) {
			if ( srq->send_type == TSNET_SEND_MEMORY ) {
				TSNET_SET_ERROR("send() is failed: (nsend: %ld, errmsg: %s, errno: %d, srq->fd: %d, srq->send_len: %lu, srq->sended_len: %lu)", nsend, strerror(errno), errno, srq->fd, srq->send_len, srq->sended_len);
			}
			else {
				TSNET_SET_ERROR("sendfile() is failed: (nsend: %ld, errmsg: %s, errno: %d, srq->fd: %d, srq->send_len: %lu, srq->sended_len: %lu)", nsend, strerror(errno), errno, srq->fd, srq->send_len, srq->sended_len);
			}
			goto out;
		}
	}
	
	if ( srq->send_len == srq->sended_len ) send_done = 1; // sending is completed

	if ( send_done ) {
		if ( tsnet->send_request_client->erase(tsnet->send_request_client, &client_fd, sizeof(client_fd), 0) < 0 ) goto out;
		
		if ( tsnet->send_request_client->empty(tsnet->send_request_client, &client_fd, sizeof(client_fd)) ) {
			if ( epoll_event_control(tsnet->epfd, EPOLL_CTL_MOD, client_fd, EPOLLIN) < 0 ) {
				TSNET_SET_ERROR("epoll_ctl(EPOLL_CTL_MOD, EPOLLIN) is failed: (errmsg: %s, errno: %d)", strerror(errno), errno);
				goto out;
			}
		}

		if ( tsnet->cb_vec[TSNET_EVENT_SEND_COMPLETE] ) tsnet->cb_vec[TSNET_EVENT_SEND_COMPLETE](tsnet, client_fd, NULL, 0);
	}

	return 0;

out:
	if ( srq->send_type == TSNET_SEND_MEMORY ) { 
		safe_free(srq->send_data); 
	}
	else if ( srq->send_type == TSNET_SEND_FILE ) { 
		safe_close(srq->sendfile_fd); 
	}

	(void)close_client(tsnet, client_fd);

	return -1;
}

static int insert_send_event(TSNET *tsnet, struct tsnet_send_request *srq)
{
	if ( tsnet->send_request_client->insert(tsnet->send_request_client, &srq->fd, sizeof(srq->fd), srq, sizeof(struct tsnet_send_request)) ) goto out;
	
	if ( epoll_event_control(tsnet->epfd, EPOLL_CTL_MOD, srq->fd, EPOLLIN | EPOLLOUT) < 0 ) {
		TSNET_SET_ERROR("epoll_ctl(EPOLL_CTL_MOD, EPOLLIN | EPOLLOUT) is failed: (errmsg: %s, errno: %d)", strerror(errno), errno);
		(void)tsnet->send_request_client->erase(tsnet->send_request_client, &srq->fd, sizeof(srq->fd), 1);
		goto out;
	}
	
	return 0;

out:
	return -1;
}

static int get_client_info(socket_t client_fd, struct tsnet_client *client)
{
	struct sockaddr_in caddr;
	socklen_t caddr_len = sizeof(caddr);

	if ( getpeername(client_fd, (struct sockaddr *)&caddr, &caddr_len) < 0 ) {
		TSNET_SET_ERROR("getpeername() is failed: (errmsg: %s, errno: %d)\n", strerror(errno), errno);
		goto out;
	}

	if ( !inet_ntop(AF_INET, &caddr.sin_addr, client->ip, sizeof(client->ip)) ) {
		TSNET_SET_ERROR("inet_ntop() is failed: (errmsg: %s, errno: %d)\n", strerror(errno), errno);
		goto out;
	}

	client->fd = client_fd;
	client->port = ntohs(caddr.sin_port);

	return 0;

out:
	return -1;
}

TSNET * tsnet_create(int type, int backlog, int max_client)
{
	TSNET *tsnet = NULL;

	switch (type) {
		case TSNET_EPOLL:
			break;
		default:
			TSNET_SET_ERROR("invalid argument: (type = %d)", type);
			goto out;
	}

	if ( !(tsnet = calloc(1, sizeof(TSNET))) ) {
		TSNET_SET_ERROR("calloc() is failed: (errmsg: %s, errno: %d, size: %d)", strerror(errno), errno, sizeof(TSNET));
		goto out;
	}

	tsnet->fd = -1;
	if ( backlog <= 0 ) tsnet->backlog = TSNET_DEFAULT_BACKLOG;
	else tsnet->backlog = backlog;
	if ( max_client <= 0 ) tsnet->max_client = TSNET_DEFAULT_MAX_CLIENT;
	else tsnet->max_client = max_client;

	if ( !(tsnet->connected_client = ht_create(0, 0, 0)) ) goto out;
	if ( !(tsnet->send_request_client = ht_create(0, 0, 1)) ) goto out;

	ht_set_erase_free(tsnet->send_request_client, send_request_erase_free);

	signal(SIGPIPE, SIG_IGN);

	return tsnet;

out:
	tsnet_delete(tsnet);

	return NULL;
}

void tsnet_set_user_data(TSNET *tsnet, void *user_data)
{
	if ( tsnet && user_data ) tsnet->user_data = user_data;
}

void tsnet_delete(TSNET *tsnet)
{
	if ( tsnet ) {
		safe_close(tsnet->fd);
		safe_close(tsnet->epfd);
		ht_delete(tsnet->connected_client);
		ht_delete(tsnet->send_request_client);
		free(tsnet);
	}
}

int tsnet_bind(TSNET *tsnet, const char *ip, uint16_t port)
{
	struct sockaddr_in saddr;
	socklen_t reuse = 1;

	memset(&saddr, 0x00, sizeof(saddr));
	
	if ( !tsnet || !ip ) {
		TSNET_SET_ERROR("invalid argument: (tsnet = %s, ip = %s)", CKNUL(tsnet), CKNUL(ip));
		goto out;
	}

	snprintf(tsnet->ip, sizeof(tsnet->ip), "%s", ip);
	tsnet->port = port;

	if ( (tsnet->fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0 ) {
		TSNET_SET_ERROR("socket() is failed: (errmsg: %s, errno: %d)", strerror(errno), errno);
		goto out;
	}

	if ( setsockopt(tsnet->fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0 ) {
		TSNET_SET_ERROR("setsockopt('SO_REUSEADDR') is failed: (errmsg: %s, errno: %d)", strerror(errno), errno);
		goto out;
	}

#ifdef SO_REUSEPORT
	reuse = 1;
	if ( setsockopt(tsnet->fd, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuse, sizeof(reuse)) < 0 ) {
		TSNET_SET_ERROR("setsockopt('SO_REUSEPORT') is failed: (errmsg: %s, errno: %d)", strerror(errno), errno);
		goto out;
	}
#endif

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(tsnet->port);
	saddr.sin_addr.s_addr = inet_addr(tsnet->ip); 

  if ( bind(tsnet->fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0 ) {
		TSNET_SET_ERROR("bind(ip:%s, port:%u) is failed: (errmsg: %s, errno: %d)", tsnet->ip, tsnet->port, strerror(errno), errno);
		goto out;
	}
	
	if ( listen(tsnet->fd, tsnet->backlog) < 0 ) {
		TSNET_SET_ERROR("listen(backlog:%u) is failed: (errmsg: %s, errno: %d)", tsnet->backlog, strerror(errno), errno);
		goto out;
	}

	if ( (tsnet->epfd = epoll_create(1)) < 0 ) {
		TSNET_SET_ERROR("epoll_create() is failed: (errmsg: %s, errno: %d)", strerror(errno), errno);
		goto out;
	}

	if ( epoll_event_control(tsnet->epfd, EPOLL_CTL_ADD, tsnet->fd, EPOLLIN) < 0 ) {
		TSNET_SET_ERROR("epoll_ctl(EPOLL_CTL_ADD, EPOLLIN) is failed: (errmsg: %s, errno: %d)", strerror(errno), errno);
		goto out;
	}
	
	tsnet->is_bind = 1;

	return 0;

out:
	return -1;
}

int tsnet_addListener(TSNET *tsnet, tsnet_event_t event, tsnet_cb_t cb)
{
	if ( !tsnet || !cb ) {
		TSNET_SET_ERROR("invalid argument: (tsnet = %s, cb = %s)", CKNUL(tsnet), CKNUL(cb));
		goto out;
	}

	switch (event) {
		case TSNET_EVENT_ACCEPT:
		case TSNET_EVENT_CLOSE:
		case TSNET_EVENT_RECV:
		case TSNET_EVENT_SEND_COMPLETE:
			break;
		default:
			TSNET_SET_ERROR("invalid tsnet event: (event = %d)", event);
			goto out;
	}

	tsnet->cb_vec[event] = cb;

	return 0;

out:
	return -1;
}

int tsnet_loop(TSNET *tsnet)
{
	uint8_t *recv_buffer = NULL;

	if ( !tsnet ) {
		TSNET_SET_ERROR("invalid argument: (tsnet = null)");
		return -1;
	}

	if ( !tsnet->is_bind) {
		TSNET_SET_ERROR("tsnet is not ready: (first call tsnet_setup())");
		return -1;
	}
	
	if ( !(recv_buffer = malloc(TSNET_MAX_RECV_BYTES)) ) {
		TSNET_SET_ERROR("malloc() is failed: (size: %d, errmsg: %s, errno: %d)", TSNET_MAX_RECV_BYTES, strerror(errno), errno);
		return -1;
	}
	
	struct epoll_event events[tsnet->max_client]; // I don't know why valgrind warns when doing dynamic allocation.

	while (1) {
		int nfds = epoll_wait(tsnet->epfd, events, tsnet->max_client, -1);

		for ( int i = 0; i < nfds; i++ ) {
			socket_t event_fd = events[i].data.fd;
			unsigned int event = events[i].events;

			if ( event_fd == tsnet->fd /* server fd */) { // accept event
				struct sockaddr_in caddr; 
				socket_t client_fd;
				socklen_t caddr_len = sizeof(caddr);

				if ( (client_fd = accept4(tsnet->fd, (struct sockaddr *)&caddr, &caddr_len, SOCK_NONBLOCK)) < 0 ) continue;

				struct tsnet_client client;
				if ( get_client_info(client_fd, &client) < 0 ) goto out;

				if ( tsnet->connected_client->insert(tsnet->connected_client, &client_fd, sizeof(client_fd), &client, sizeof(client)) < 0 ) goto out;

				if ( epoll_event_control(tsnet->epfd, EPOLL_CTL_ADD, client_fd, EPOLLIN) < 0 ) {
					TSNET_SET_ERROR("epoll_ctl(EPOLL_CTL_ADD, EPOLLIN) is failed: (errmsg: %s, errno: %d)", strerror(errno), errno);
					goto out;
				}

				if ( tsnet->cb_vec[TSNET_EVENT_ACCEPT] ) tsnet->cb_vec[TSNET_EVENT_ACCEPT](tsnet, client_fd, NULL, 0);
			}
			else {
				// recv event
				if ( event & EPOLLIN ) {
					memset(recv_buffer, 0x00, TSNET_MAX_RECV_BYTES);

					ssize_t nrecv = recv(event_fd, recv_buffer, TSNET_MAX_RECV_BYTES, 0);
					if ( nrecv <= 0 ) {
						if ( close_client(tsnet, event_fd) < 0 ) goto out;
					}
					else {
						if ( tsnet->cb_vec[TSNET_EVENT_RECV] ) tsnet->cb_vec[TSNET_EVENT_RECV](tsnet, event_fd, recv_buffer, nrecv);
					}
				}
				// hang-up or error event
				else if ( event & EPOLLHUP /* recv return zero(same case) */ || event & EPOLLERR ) {
					if ( close_client(tsnet, event_fd) < 0 ) goto out;
				}
				// send event
				else if ( event & EPOLLOUT ) {
					HashTableBucket *bucket;

					if ( !(bucket = tsnet->send_request_client->find(tsnet->send_request_client, &event_fd, sizeof(event_fd))) ) {
						TSNET_SET_ERROR("PANIC: EPOLLOUT event but can't find file descriptor");
						goto out;
					}

					if ( send_data_to_client(tsnet, bucket, event_fd) < 0 ) goto out;
					//goto out; // memory leak test
				}
			}
		}
	}

	safe_free(recv_buffer);

	return 0;

out:
	safe_free(recv_buffer);

	return -1;
}

int tsnet_send(TSNET *tsnet, socket_t client_fd, const void *data, size_t data_len)
{
	struct tsnet_send_request srq;
	
	memset(&srq, 0x00, sizeof(srq));

	if ( !tsnet || client_fd < 0 || !data || data_len == 0 ) {
		TSNET_SET_ERROR("invalid argument: (tsnet = %s, client_fd = %d, data = %s, data_len = %lu", CKNUL(tsnet), client_fd, CKNUL(data), data_len);
		goto out;
	}

	if ( !(srq.send_data = malloc(data_len)) ) {
		TSNET_SET_ERROR("malloc() is failed: (errmsg: %s, errno: %d, size: %d)", strerror(errno), errno, data_len);
		goto out;
	}
	
	memcpy(srq.send_data, data, data_len);

	srq.fd = client_fd;
	srq.send_type = TSNET_SEND_MEMORY;
	srq.send_len = data_len;
	srq.sended_len = 0;

	if ( insert_send_event(tsnet, &srq) < 0 ) goto out;
	
	return 0;

out:
	safe_free(srq.send_data);

	return -1;
}

int tsnet_sendfile(TSNET *tsnet, socket_t client_fd, const char *file_path)
{
	struct stat st;
	struct tsnet_send_request srq;
	
	memset(&srq, 0x00, sizeof(srq));
	
	if ( !tsnet || client_fd < 0 || !file_path ) {
		TSNET_SET_ERROR("invalid argument: (tsnet = %s, client_fd = %d, file_path = %s", CKNUL(tsnet), client_fd, CKNUL(file_path));
		goto out;
	}

	if ( (srq.sendfile_fd = open(file_path, O_RDONLY)) < 0 ) {
		TSNET_SET_ERROR("open() is failed: (errmsg: %s, errno: %d, path: %s)\n", strerror(errno), errno, file_path);
		goto out;
	}

	if ( fstat(srq.sendfile_fd, &st) < 0 ) {
		TSNET_SET_ERROR("fstat() is failed: (errmsg: %s, errno: %d, path: %s)\n", strerror(errno), errno, file_path);
		goto out;
	}

	srq.fd = client_fd;
	srq.send_type = TSNET_SEND_FILE;
	srq.send_len = st.st_size;
	srq.sended_len = 0;

	if ( insert_send_event(tsnet, &srq) < 0 ) goto out;
	
	return 0;

out:
	return -1;
}

int tsnet_get_client_info(TSNET *tsnet, socket_t client_fd, struct tsnet_client *client)
{
	HashTableBucket *bucket;

	if ( !(bucket = tsnet->connected_client->find(tsnet->connected_client, &client_fd, sizeof(client_fd))) ) {
		TSNET_SET_ERROR("can not found (connected_client: fd = %d)", client_fd);
		goto out;
	}

	memcpy(client, bucket->value, bucket->value_len);

	return 0;

out:
	return -1;
}

const char *tsnet_get_last_error()
{
	return tsnet_last_error;
}
