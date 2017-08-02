cmake_minimum_required(VERSION 2.6)

PROJECT(nlbwmon C)
ADD_DEFINITIONS(-Os -Wall -Werror --std=gnu99 -g3 -Wmissing-declarations -D_GNU_SOURCE)

SET(SOURCES
	client.c database.c neigh.c nfnetlink.c
	nlbwmon.c protocol.c socket.c subnets.c
	timing.c utils.c)

ADD_EXECUTABLE(nlbwmon ${SOURCES})
TARGET_LINK_LIBRARIES(nlbwmon ubox nl-tiny z)

SET(CMAKE_INSTALL_PREFIX /usr)

INSTALL(TARGETS nlbwmon RUNTIME DESTINATION sbin)
