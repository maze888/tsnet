CMAKE_MINIMUM_REQUIRED (VERSION 2.8)

PROJECT (simple_http_server)

INCLUDE_DIRECTORIES (..)

LINK_DIRECTORIES (..)
LINK_LIBRARIES (tsnet)

#ADD_COMPILE_OPTIONS (-Wall -O --std=c99)
ADD_COMPILE_OPTIONS (-Wall -g --std=c99)

ADD_DEFINITIONS (-D_DEFAULT_SOURCE)

SET (SRCS simple_http_server.c picohttpparser.c sha256.c)

ADD_EXECUTABLE (simple_http_server ${SRCS})
