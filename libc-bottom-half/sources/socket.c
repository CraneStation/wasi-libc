#include <errno.h>
#include <netinet/in.h>
#include <wasi/descriptor_table.h>
#include <wasi/report-error.h>
#include <wasi/sockets_utils.h>
#include <wasi/tcp.h>
#include <wasi/udp.h>

#ifdef __wasip2__
#define sockets_static_tcp_socket_create tcp_create_socket_create_tcp_socket
#define sockets_static_udp_socket_create udp_create_socket_create_udp_socket
#define SOCKETS_IP_ADDRESS_FAMILY_IPV4 NETWORK_IP_ADDRESS_FAMILY_IPV4
#define SOCKETS_IP_ADDRESS_FAMILY_IPV6 NETWORK_IP_ADDRESS_FAMILY_IPV6
#endif

#ifndef __webp2__
static int tcp_socket(sockets_ip_address_family_t family, bool blocking) {
  sockets_error_code_t error;
  sockets_own_tcp_socket_t socket;
  if (!sockets_static_tcp_socket_create(family, &socket, &error))
    return __wasilibc_socket_error_to_errno(&error);

  return __wasilibc_add_tcp_socket(socket, family, blocking);
}

static int udp_socket(sockets_ip_address_family_t family, bool blocking) {
  sockets_error_code_t error;
  sockets_own_udp_socket_t socket;
  if (!sockets_static_udp_socket_create(family, &socket, &error))
    return __wasilibc_socket_error_to_errno(&error);

  return __wasilibc_add_udp_socket(socket, family, blocking);
}
#endif // !webp2

int socket(int domain, int type, int protocol) {
#if defined(__wasip1__) || defined(__wasip2__) || defined(__wasip3__)
  sockets_ip_address_family_t family;
  switch (domain) {
  case PF_INET:
    family = SOCKETS_IP_ADDRESS_FAMILY_IPV4;
    break;

  case PF_INET6:
    family = SOCKETS_IP_ADDRESS_FAMILY_IPV6;
    break;

  default:
    errno = EAFNOSUPPORT;
    return -1;
  }

  int real_type = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
  bool blocking = (type & SOCK_NONBLOCK) == 0;
  // Ignore SOCK_CLOEXEC flag. That concept does not exist in WASI.

  if (real_type == SOCK_STREAM && (protocol == 0 || protocol == IPPROTO_TCP)) {
    return tcp_socket(family, blocking);

  } else if (real_type == SOCK_DGRAM &&
             (protocol == 0 || protocol == IPPROTO_UDP)) {
    return udp_socket(family, blocking);

  } else {
    errno = EPROTONOSUPPORT;
    return -1;
  }
#elif defined(__webp2__)
  (void)domain;
  (void)type;
  (void)protocol;
  WASILIBC_LOG_UNSUPPORTED("socket");
  errno = ENOSYS;
  return -1;
#else
#error "Unknown WASI version"
#endif
}
