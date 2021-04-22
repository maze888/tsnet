# tsnet
tsnet is simple server network library    

* single thread loop
* event driven


# example (echo server)
```c
static void accept_cb(socket_t client_fd, uint8_t *data, ssize_t data_len)
{
  struct tsnet_client client;
  (void)tsnet_get_client_info(client_fd, &client);    
  printf("connected with %s:%d\n", client.ip, client.port);
}

static void read_cb(socket_t client_fd, uint8_t *data, ssize_t data_len)
{
  (void)send(client_fd, data, data_len, 0);
}

static void close_cb(socket_t client_fd, uint8_t *data, ssize_t data_len)
{
  struct tsnet_client client;
  (void)tsnet_get_client_info(client_fd, &client);
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

  (void)tsnet_addListener(tsnet, TSNET_EVENT_ACCEPT, accept_cb);
  (void)tsnet_addListener(tsnet, TSNET_EVENT_READ, read_cb);
  (void)tsnet_addListener(tsnet, TSNET_EVENT_CLOSE, close_cb);
    
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
