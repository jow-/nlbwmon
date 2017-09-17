cmake_minimum_required(VERSION 2.6)

PROJECT(nlbwmon C)
ADD_DEFINITIONS(-Os -Wall -Werror --std=gnu99 -g3 -Wmissing-declarations -D_GNU_SOURCE)

OPTION(LIBNL_LIBRARY_TINY "Use LEDE/OpenWrt libnl-tiny" OFF)

SET(SOURCES
	client.c database.c neigh.c nfnetlink.c
	nlbwmon.c protocol.c socket.c subnets.c
	timing.c utils.c)

ADD_EXECUTABLE(nlbwmon ${SOURCES})

IF(LIBNL_LIBRARY_TINY)
  TARGET_LINK_LIBRARIES(nlbwmon nl-tiny)
ELSE()
  FIND_LIBRARY(LIBNL_LIBRARY NAMES nl-3 nl)
  FIND_LIBRARY(LIBNL_GENL_LIBRARY NAMES nl-genl-3 nl-genl)
  TARGET_LINK_LIBRARIES(nlbwmon ${LIBNL_LIBRARY} ${LIBNL_GENL_LIBRARY})
  FIND_PATH(LIBNL_LIBRARY_INCLUDE_DIR NAMES netlink/netlink.h PATH_SUFFIXES libnl3)
  INCLUDE_DIRECTORIES(${LIBNL_LIBRARY_INCLUDE_DIR})
ENDIF()

TARGET_LINK_LIBRARIES(nlbwmon ubox z)

SET(CMAKE_INSTALL_PREFIX /usr)

INSTALL(TARGETS nlbwmon RUNTIME DESTINATION sbin)
