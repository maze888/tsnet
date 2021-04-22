#include "tsnet_epoll.h"

int epoll_event_control(int epfd, int op, int fd, uint32_t events)
{
	struct epoll_event ev;

	ev.events = events;
	ev.data.fd = fd;
					
	return events ? epoll_ctl(epfd, op, fd, &ev) : epoll_ctl(epfd, op, fd, NULL);
}
