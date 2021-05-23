# tsnet
tsnet is simple server network library    

* single thread server
* complete nonblocking
* event driven
* only support tcp(if necessary UDP support add)

# example (echo server)
```c
#include "tsnet.h"

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
  if ( tsnet_send(tsnet, client_fd, data, data_len) < 0 ) {
    fprintf(stderr, "%s\n", tsnet_get_last_error());
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

  if ( argc != 2 ) {
    fprintf(stderr, "%s (port)\n", argv[0]);
    return 1;
  }

  tsnet = tsnet_create(TSNET_EPOLL, 0, 0);
  if ( !tsnet ) {
    fprintf(stderr, "%s\n", tsnet_get_last_error());
    goto out;
  }

  if ( tsnet_bind(tsnet, "0.0.0.0", atoi(argv[1])) < 0 ) {
    fprintf(stderr, "%s\n", tsnet_get_last_error());
    goto out;
  }

  if ( tsnet_addListener(tsnet, TSNET_EVENT_ACCEPT, accept_cb) < 0 ) {
    fprintf(stderr, "%s\n", tsnet_get_last_error());
    goto out;
  }
  if ( tsnet_addListener(tsnet, TSNET_EVENT_RECV, recv_cb) < 0 ) {
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
```
