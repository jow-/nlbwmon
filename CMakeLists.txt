cmake_minimum_required(VERSION 3.0)
include(CheckFunctionExists)

project(nlbwmon C)
add_definitions(-Os -Wall -Werror --std=gnu99 -g3 -Wmissing-declarations -D_GNU_SOURCE)

option(LIBNL_LIBRARY_TINY "Use LEDE/OpenWrt libnl-tiny" OFF)

set(SOURCES
	client.c database.c neigh.c nfnetlink.c
	nlbwmon.c protocol.c socket.c subnets.c
	timing.c utils.c)

add_executable(nlbwmon ${SOURCES})

if(LIBNL_LIBRARY_TINY)
  target_link_libraries(nlbwmon nl-tiny)
else()
  find_library(LIBNL_LIBRARY NAMES nl-3 nl)
  find_library(LIBNL_GENL_LIBRARY NAMES nl-genl-3 nl-genl)
  target_link_libraries(nlbwmon ${LIBNL_LIBRARY} ${LIBNL_GENL_LIBRARY})
  find_path(LIBNL_LIBRARY_INCLUDE_DIR NAMES netlink/netlink.h PATH_SUFFIXES libnl3)
  include_directories(${LIBNL_LIBRARY_INCLUDE_DIR})
endif()

set(CMAKE_REQUIRED_LIBRARIES ubox)
check_function_exists(uloop_interval_set INTERVAL_FUNCTION_EXISTS)
unset(CMAKE_REQUIRED_LIBRARIES)

if (INTERVAL_FUNCTION_EXISTS)
  add_definitions(-DHAVE_ULOOP_INTERVAL)
endif()

target_link_libraries(nlbwmon ubox z)

set(CMAKE_INSTALL_PREFIX /usr)

install(TARGETS nlbwmon RUNTIME DESTINATION sbin)
