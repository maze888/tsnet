#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define CKNUL(p) p ? "valid" : "null"
#define safe_free(p) if ( p ) { free(p); p = NULL; }
#define safe_close(fd) if ( fd >= 0 ) { close(fd); fd = -1; }

#define TSNET_DEFAULT_BACKLOG 64
#define TSNET_DEFAULT_MAX_CLIENT 1024
#define TSNET_MAX_READ_BYTES (BUFSIZ * 16)

#define TSNET_SET_ERROR(...) tsnet_set_last_error(__FILE__, __LINE__, __func__, __VA_ARGS__);

extern char tsnet_last_error[BUFSIZ];

void tsnet_set_last_error(const char *file, int line, const char *func, char *fmt, ...);
