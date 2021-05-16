#include "tsnet.h"
#include "tsnet_socket.h"

int tsnet_block(socket_t fd)
{
	int opts;

	if ( (opts = fcntl(fd, F_GETFL)) < 0 ) {
		TSNET_SET_ERROR("fcntl(F_GETFL) is failed: (errmsg: %s, errno: %d)", strerror(errno), errno);
		return -1;
	}

	opts &= ~O_NONBLOCK;
	if ( fcntl(fd, F_SETFL, opts) < 0 ) {
		TSNET_SET_ERROR("fcntl(F_SETFL) is failed: (errmsg: %s, errno: %d)", strerror(errno), errno);
		return -1;
	}

	return opts;
}

int tsnet_nonblock(socket_t fd)
{
	int opts;

	if ( (opts = fcntl(fd, F_GETFL)) < 0 ) {
		TSNET_SET_ERROR("fcntl(F_GETFL) is failed: (errmsg: %s, errno: %d)", strerror(errno), errno);
		return -1;
	}
	
	opts = (opts | O_NONBLOCK);
	if ( fcntl(fd, F_SETFL, opts) < 0 ) {
		TSNET_SET_ERROR("fcntl(F_SETFL) is failed: (errmsg: %s, errno: %d)", strerror(errno), errno);
		return -1;
	}
	
	return opts;
}
