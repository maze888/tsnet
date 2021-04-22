#include "tsnet.h"
#include "tsnet_common_inter.h"
#include "tsnet_epoll.h"

static int close_client(TSNET *tsnet, int client_fd)
{
	if ( epoll_event_control(tsnet->epfd, EPOLL_CTL_DEL, client_fd, 0) < 0 ) {
		TSNET_SET_ERROR("epoll_ctl(EPOLL_CTL_DEL) is failed: (errmsg: %s, errno: %d)", strerror(errno), errno);
		goto out;
	}
					
	if ( tsnet->cb_vec[TSNET_EVENT_CLOSE] ) tsnet->cb_vec[TSNET_EVENT_CLOSE](client_fd, NULL, 0);
	
	safe_close(client_fd);
						
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
		TSNET_SET_ERROR("malloc() is failed: (errmsg: %s, errno: %d, size: %d)", strerror(errno), errno, sizeof(TSNET));
		goto out;
	}

	tsnet->fd = -1;
	if ( backlog <= 0 ) tsnet->backlog = TSNET_DEFAULT_BACKLOG;
	else tsnet->backlog = backlog;
	if ( max_client <= 0 ) tsnet->max_client = TSNET_DEFAULT_MAX_CLIENT;
	else tsnet->max_client = max_client;

	signal(SIGPIPE, SIG_IGN);

	return tsnet;

out:
	tsnet_delete(tsnet);

	return NULL;
}

void tsnet_delete(TSNET *tsnet)
{
	if ( tsnet ) {
		safe_close(tsnet->fd);
		safe_close(tsnet->epfd);
		free(tsnet);
	}
}

int tsnet_setup(TSNET *tsnet, const char *ip, uint16_t port)
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

	if ( (tsnet->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
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
		TSNET_SET_ERROR("epoll_ctl() is failed: (errmsg: %s, errno: %d)", strerror(errno), errno);
		goto out;
	}
	
	tsnet->is_setup = 1;

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
		case TSNET_EVENT_READ:
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
	uint8_t *read_buffer = NULL;
	struct epoll_event *events = NULL;

	if ( !tsnet ) {
		TSNET_SET_ERROR("invalid argument: (tsnet = null)");
		goto out;
	}

	if ( !tsnet->is_setup ) {
		TSNET_SET_ERROR("tsnet is not ready: (first call tsnet_setup())");
		goto out;
	}
	
	if ( !(events = malloc(tsnet->max_client)) ) {
		TSNET_SET_ERROR("malloc() is failed: (size: %d, errmsg: %s, errno: %d)", tsnet->max_client, strerror(errno), errno);
		goto out;
	}

	if ( !(read_buffer = malloc(TSNET_MAX_READ_BYTES)) ) {
		TSNET_SET_ERROR("malloc() is failed: (size: %d, errmsg: %s, errno: %d)", TSNET_MAX_READ_BYTES, strerror(errno), errno);
		goto out;
	}

	while (1) {
		int nfds = epoll_wait(tsnet->epfd, events, tsnet->max_client, -1);
		for ( int i = 0; i < nfds; i++ ) {
			if ( events[i].data.fd == tsnet->fd ) {
				struct sockaddr_in caddr; 
				socklen_t caddr_len = sizeof(caddr);
				socket_t client_fd = accept(tsnet->fd, (struct sockaddr *)&caddr, &caddr_len);
				if ( client_fd < 0 ) continue;
				
				if ( epoll_event_control(tsnet->epfd, EPOLL_CTL_ADD, client_fd, EPOLLIN) < 0 ) {
					TSNET_SET_ERROR("epoll_ctl(EPOLL_CTL_ADD) is failed: (errmsg: %s, errno: %d)", strerror(errno), errno);
					goto out;
				}

				if ( tsnet->cb_vec[TSNET_EVENT_ACCEPT] ) tsnet->cb_vec[TSNET_EVENT_ACCEPT](client_fd, NULL, 0);
			}
			else if ( events[i].events & EPOLLIN ) {
				memset(read_buffer, 0x00, TSNET_MAX_READ_BYTES);

				ssize_t nread = recv(events[i].data.fd, read_buffer, TSNET_MAX_READ_BYTES, 0);
				if ( nread <= 0 ) {
					if ( close_client(tsnet, events[i].data.fd) < 0 ) goto out;
				}
				else {
					if ( tsnet->cb_vec[TSNET_EVENT_READ] ) tsnet->cb_vec[TSNET_EVENT_READ](events[i].data.fd, read_buffer, nread);
				}
			}
			else if ( events[i].events & EPOLLHUP ) { // recv return zero (same case)
				if ( close_client(tsnet, events[i].data.fd) < 0 ) goto out;
			}
		}
	}

	safe_free(events);
	safe_free(read_buffer);

	return 0;

out:
	safe_free(events);
	safe_free(read_buffer);

	return -1;
}

int tsnet_get_client_info(socket_t client_fd, struct tsnet_client *client)
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

const char *tsnet_get_last_error()
{
	return tsnet_last_error;
}
