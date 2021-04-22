CMAKE_MINIMUM_REQUIRED (VERSION 2.8)

PROJECT (tsnet)

#ADD_COMPILE_OPTIONS (-Wall -O --std=c99)
ADD_COMPILE_OPTIONS (-Wall -g --std=c99)

ADD_DEFINITIONS (-D_DEFAULT_SOURCE)

SET (SRCS tsnet.c tsnet_common_inter.c tsnet_epoll.c)

ADD_LIBRARY (tsnet SHARED ${SRCS})
