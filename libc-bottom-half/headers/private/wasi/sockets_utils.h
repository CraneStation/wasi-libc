#ifndef __wasi_sockets_utils_h
#define __wasi_sockets_utils_h

#include <netinet/in.h>
#include <wasi/api.h>
#include <wasi/descriptor_table.h>

#if !defined(__wasip1__) && !defined(__webp2__)

#if defined(__wasip2__)
typedef network_error_code_t sockets_error_code_t;
typedef network_ip_address_family_t sockets_ip_address_family_t;
typedef network_ip_socket_address_t sockets_ip_socket_address_t;
typedef network_ip_socket_address_t sockets_ip_socket_address_t;
typedef network_ip_address_t sockets_ip_address_t;
typedef network_ipv4_address_t sockets_ipv4_address_t;
typedef network_ipv6_address_t sockets_ipv6_address_t;

#define sockets_error_code_t network_error_code_t
#define sockets_ip_address_family_t network_ip_address_family_t
#define sockets_ip_socket_address_t network_ip_socket_address_t

#define SOCKETS_IP_ADDRESS_FAMILY_IPV4 NETWORK_IP_ADDRESS_FAMILY_IPV4
#define SOCKETS_IP_ADDRESS_FAMILY_IPV6 NETWORK_IP_ADDRESS_FAMILY_IPV6
#define SOCKETS_IP_ADDRESS_IPV4 NETWORK_IP_SOCKET_ADDRESS_IPV4
#define SOCKETS_IP_ADDRESS_IPV6 NETWORK_IP_SOCKET_ADDRESS_IPV6
#elif defined(__wasip3__)
// already defines `sockets_*` types
#else
#error "Unsupported WASI version"
#endif

typedef struct {
  enum {
    OUTPUT_SOCKADDR_NULL,
    OUTPUT_SOCKADDR_V4,
    OUTPUT_SOCKADDR_V6,
  } tag;
  union {
    struct {
      int dummy;
    } null;
    struct {
      struct sockaddr_in *addr;
      socklen_t *addrlen;
    } v4;
    struct {
      struct sockaddr_in6 *addr;
      socklen_t *addrlen;
    } v6;
  };
} output_sockaddr_t;

typedef struct {
  char *s_name;
  uint16_t port;
  uint16_t protocol;
} service_entry_t;

typedef enum {
  SERVICE_PROTOCOL_TCP = 1,
  SERVICE_PROTOCOL_UDP = 2
} service_protocol_e;

#ifdef __wasip2__
network_borrow_network_t __wasi_sockets_utils__borrow_network();
#endif

int __wasilibc_map_socket_error(sockets_error_code_t *wasi_error);

static inline int
__wasilibc_socket_error_to_errno(sockets_error_code_t *wasi_error) {
  errno = __wasilibc_map_socket_error(wasi_error);
  return -1;
}

// Conversion from a libc-based `sockaddr` + `socklen_t` to a WASI address.
int __wasilibc_sockaddr_to_wasi(sockets_ip_address_family_t expected_family,
                                const struct sockaddr *address, socklen_t len,
                                sockets_ip_socket_address_t *result);

// Conversion from a WASI address to a libc-based `sockaddr`.
//
// This is done in two stages. First the user-provided `sockaddr` is validated
// along with its lenght to ensure it's large enough to hold `expected_family`.
// Next once the address is available `__wasilibc_wasi_to_sockaddr` is used
// to write the output value.
int __wasilibc_sockaddr_validate(sockets_ip_address_family_t expected_family,
                                 struct sockaddr *addr, socklen_t *addrlen,
                                 output_sockaddr_t *result);
void __wasilibc_wasi_to_sockaddr(const sockets_ip_socket_address_t input,
                                 output_sockaddr_t *output);

int __wasilibc_getsockopt_timeout(monotonic_clock_duration_t timeout,
                                  void *optval, socklen_t *optlen);
int __wasilibc_setsockopt_timeout(const void *optval, socklen_t optlen,
                                  monotonic_clock_duration_t *timeout);

int __wasilibc_wasi_family_to_libc(sockets_ip_address_family_t wasi_family);
void __wasilibc_unspecified_addr(sockets_ip_address_family_t family,
                                 sockets_ip_socket_address_t *out);
int __wasilibc_parse_port(const char *port);
const service_entry_t *__wasilibc_get_service_entry_by_name(const char *name);
const service_entry_t *
__wasilibc_get_service_entry_by_port(const uint16_t port);

#endif // !__wasip1__ && !__webp2__

#endif // __wasi_sockets_utils_h
