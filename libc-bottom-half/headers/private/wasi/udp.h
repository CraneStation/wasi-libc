#ifndef WASI_UDP_H
#define WASI_UDP_H

#include <wasi/version.h>

#if !defined(__wasip1__) && !defined(__webp2__)

#include <wasi/sockets_utils.h>

// Normalize names on WASIp2 to the WASIp3-based names
#ifdef __wasip2__
typedef udp_own_udp_socket_t sockets_own_udp_socket_t;
typedef udp_borrow_udp_socket_t sockets_borrow_udp_socket_t;
#define sockets_udp_socket_drop_own udp_udp_socket_drop_own
#define sockets_borrow_udp_socket udp_borrow_udp_socket
#endif // __wasip2__

/// Adds the provided UDP socket to the descriptor table, returning the
/// corresponding file descriptor.
int __wasilibc_add_udp_socket(sockets_own_udp_socket_t socket,
                              sockets_ip_address_family_t family,
                              bool blocking);

#endif // !__wasip1__ && !__webp2__

#endif // WASI_UDP_H
