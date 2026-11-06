#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stddefer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wasi/file_utils.h>
#include <wasi/sockets_utils.h>

#ifdef __wasip1__
static_assert(SOCK_DGRAM == __WASI_FILETYPE_SOCKET_DGRAM, "value mismatch");
static_assert(SOCK_STREAM == __WASI_FILETYPE_SOCKET_STREAM, "value mismatch");
#endif

#ifdef __wasip2__
#define IP_NAME_LOOKUP_ERROR_CODE_NAME_UNRESOLVABLE                            \
  NETWORK_ERROR_CODE_NAME_UNRESOLVABLE
#define IP_NAME_LOOKUP_ERROR_CODE_TEMPORARY_RESOLVER_FAILURE                   \
  NETWORK_ERROR_CODE_TEMPORARY_RESOLVER_FAILURE
#define IP_NAME_LOOKUP_ERROR_CODE_PERMANENT_RESOLVER_FAILURE                   \
  NETWORK_ERROR_CODE_PERMANENT_RESOLVER_FAILURE
#endif

// `<netdb.h>` may define `h_errno` as a call to `__h_errno_location`, so undo
// that here where the thread-local it ultimately refers to is defined.
#undef h_errno
_Thread_local int h_errno = 0;

#if !defined(__wasip1__) && !defined(__wasip2__)
int *__h_errno_location(void) { return &h_errno; }
#endif

static struct servent global_serv = {0};

static int map_error(ip_name_lookup_error_code_t *error) {
#ifdef __wasip2__
  switch (*error) {
#else
  switch (error->tag) {
#endif
#ifdef __wasip2__
  case NETWORK_ERROR_CODE_OUT_OF_MEMORY:
    return EAI_MEMORY;
#endif
  case IP_NAME_LOOKUP_ERROR_CODE_NAME_UNRESOLVABLE:
    return EAI_NONAME;
  case IP_NAME_LOOKUP_ERROR_CODE_TEMPORARY_RESOLVER_FAILURE:
    return EAI_AGAIN;
  case IP_NAME_LOOKUP_ERROR_CODE_PERMANENT_RESOLVER_FAILURE:
    return EAI_FAIL;
#ifdef __wasip3__
  case IP_NAME_LOOKUP_ERROR_CODE_OTHER:
    if (error->val.other.is_some) {
      wasip3_string_free(&error->val.other.val);
      error->val.other.is_some = false;
    }
    errno = EIO;
    return EAI_SYSTEM;
  case IP_NAME_LOOKUP_ERROR_CODE_ACCESS_DENIED:
    errno = EACCES;
    return EAI_SYSTEM;
  case IP_NAME_LOOKUP_ERROR_CODE_INVALID_ARGUMENT:
    errno = EINVAL;
    return EAI_SYSTEM;
#endif

  default:
#ifdef __wasip2__
    __wasilibc_socket_error_to_errno(error);
#endif
    return EAI_SYSTEM;
  }
}

static bool add_addr(sockets_ip_address_t address, in_port_t port, int socktype,
                     const struct addrinfo *restrict hint,
                     struct addrinfo **restrict current,
                     struct addrinfo **restrict result) {
  int family;
  struct sockaddr_storage addr;
  socklen_t addrlen;

  switch (address.tag) {
  case SOCKETS_IP_ADDRESS_IPV4: {
    if (hint && hint->ai_family != AF_UNSPEC && hint->ai_family != AF_INET)
      return true;
    sockets_ipv4_address_t *ip = &address.val.ipv4;
    family = PF_INET;
    addrlen = sizeof(struct sockaddr_in);
    struct sockaddr_in *addr4 = (struct sockaddr_in *)&addr;
    addr4->sin_family = AF_INET;
    addr4->sin_port = port;
    addr4->sin_addr.s_addr =
        ip->f0 | (ip->f1 << 8) | (ip->f2 << 16) | (ip->f3 << 24);
    break;
  }
  case SOCKETS_IP_ADDRESS_IPV6: {
    if (hint && hint->ai_family != AF_UNSPEC && hint->ai_family != AF_INET6)
      return true;

    sockets_ipv6_address_t *ip = &address.val.ipv6;
    family = PF_INET6;
    addrlen = sizeof(struct sockaddr_in6);
    struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&addr;
    addr6->sin6_family = AF_INET6;
    addr6->sin6_port = port;
    addr6->sin6_addr.s6_addr[0] = ip->f0 >> 8;
    addr6->sin6_addr.s6_addr[1] = ip->f0 & 0xFF;
    addr6->sin6_addr.s6_addr[2] = ip->f1 >> 8;
    addr6->sin6_addr.s6_addr[3] = ip->f1 & 0xFF;
    addr6->sin6_addr.s6_addr[4] = ip->f2 >> 8;
    addr6->sin6_addr.s6_addr[5] = ip->f2 & 0xFF;
    addr6->sin6_addr.s6_addr[6] = ip->f3 >> 8;
    addr6->sin6_addr.s6_addr[7] = ip->f3 & 0xFF;
    addr6->sin6_addr.s6_addr[8] = ip->f4 >> 8;
    addr6->sin6_addr.s6_addr[9] = ip->f4 & 0xFF;
    addr6->sin6_addr.s6_addr[10] = ip->f5 >> 8;
    addr6->sin6_addr.s6_addr[11] = ip->f5 & 0xFF;
    addr6->sin6_addr.s6_addr[12] = ip->f6 >> 8;
    addr6->sin6_addr.s6_addr[13] = ip->f6 & 0xFF;
    addr6->sin6_addr.s6_addr[14] = ip->f7 >> 8;
    addr6->sin6_addr.s6_addr[15] = ip->f7 & 0xFF;
    addr6->sin6_flowinfo = 0;
    addr6->sin6_scope_id = 0;
    break;
  }
  default: /* unreachable */
    abort();
  }

  struct addrinfo *node = malloc(sizeof(struct addrinfo));
  struct sockaddr *addr_ret = malloc(addrlen);
  if (node == NULL || addr_ret == NULL) {
    free(node);
    free(addr_ret);
    return false;
  }

  memcpy(addr_ret, &addr, addrlen);

  *node = (struct addrinfo){
      .ai_family = family,
      .ai_flags = 0,
      .ai_socktype = socktype,
      .ai_protocol = socktype == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP,
      .ai_addrlen = addrlen,
      .ai_addr = addr_ret,
      .ai_canonname = NULL,
      .ai_next = NULL,
  };

  if (*result == NULL)
    *result = node;
  if (*current)
    (*current)->ai_next = node;
  *current = node;

  return true;
}

static bool set_global_serv_entry(const service_entry_t *entry,
                                  const char *proto) {
  if (!entry) {
    return false; // Service not found
  }

  global_serv.s_name = entry->s_name;
  global_serv.s_port = htons(entry->port);
  global_serv.s_aliases = NULL;

  // If proto is NULL then any protocol is matched
  if ((!proto || strcmp(proto, "tcp") == 0) &&
      entry->protocol & SERVICE_PROTOCOL_TCP) {
    global_serv.s_proto = "tcp";
  } else if ((!proto || strcmp(proto, "udp") == 0) &&
             entry->protocol & SERVICE_PROTOCOL_UDP) {
    global_serv.s_proto = "udp";
  } else {
    return false; // Protocol not supported
  }

  return true;
}

static sockets_ip_address_t ipv4_address(const uint8_t *b) {
  return (sockets_ip_address_t){
      .tag = SOCKETS_IP_ADDRESS_IPV4,
      .val = {.ipv4 = {.f0 = b[0], .f1 = b[1], .f2 = b[2], .f3 = b[3]}},
  };
}

static sockets_ip_address_t ipv6_address(const uint8_t *b) {
  return (sockets_ip_address_t){
      .tag = SOCKETS_IP_ADDRESS_IPV6,
      .val = {.ipv6 =
                  {
                      .f0 = (uint16_t)(b[0] << 8 | b[1]),
                      .f1 = (uint16_t)(b[2] << 8 | b[3]),
                      .f2 = (uint16_t)(b[4] << 8 | b[5]),
                      .f3 = (uint16_t)(b[6] << 8 | b[7]),
                      .f4 = (uint16_t)(b[8] << 8 | b[9]),
                      .f5 = (uint16_t)(b[10] << 8 | b[11]),
                      .f6 = (uint16_t)(b[12] << 8 | b[13]),
                      .f7 = (uint16_t)(b[14] << 8 | b[15]),
                  }},
  };
}

int getaddrinfo(const char *restrict host, const char *restrict serv,
                const struct addrinfo *restrict hint,
                struct addrinfo **restrict res) {
  *res = NULL;

  int flags = hint ? hint->ai_flags : 0;
  if (flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_V4MAPPED |
                AI_ALL | AI_ADDRCONFIG | AI_NUMERICSERV))
    return EAI_BADFLAGS;

  int family = hint ? hint->ai_family : AF_UNSPEC;
  if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6)
    return EAI_FAMILY;

  int socktype = hint ? hint->ai_socktype : 0;
  if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM)
    return EAI_SOCKTYPE;

  if (host == NULL && serv == NULL)
    return EAI_NONAME;

  // The 'serv' parameter can be either a port number or a service name. By
  // default an entry is generated for both TCP and UDP; a service table
  // entry and/or the socktype hint narrow that down.
  int port = 0;
  uint16_t protocol = SERVICE_PROTOCOL_TCP | SERVICE_PROTOCOL_UDP;
  if (serv != NULL) {
    port = __wasilibc_parse_port(serv);
    if (port < 0) {
      if (flags & AI_NUMERICSERV)
        return EAI_NONAME;
      const service_entry_t *service =
          __wasilibc_get_service_entry_by_name(serv);
      if (service == NULL)
        return EAI_NONAME;
      port = service->port;
      protocol = service->protocol;
    }
  }
  if (socktype == SOCK_STREAM)
    protocol &= SERVICE_PROTOCOL_TCP;
  else if (socktype == SOCK_DGRAM)
    protocol &= SERVICE_PROTOCOL_UDP;
  if (protocol == 0)
    return EAI_SERVICE;

  // Handle addresses which don't require the resolver: a NULL host maps to
  // the wildcard (AI_PASSIVE) or loopback address locally, and numeric
  // literals are parsed directly. Only fall through to the resolver for a
  // non-numeric hostname, and only when AI_NUMERICHOST permits it.
  sockets_ip_address_t local[2];
  size_t num_local = 0;
  struct in_addr a4;
  struct in6_addr a6;
  if (host == NULL) {
    if (family != AF_INET6) {
      static const uint8_t any4[4] = {0, 0, 0, 0};
      static const uint8_t loopback4[4] = {127, 0, 0, 1};
      local[num_local++] = ipv4_address(flags & AI_PASSIVE ? any4 : loopback4);
    }
    if (family != AF_INET) {
      const struct in6_addr *a =
          flags & AI_PASSIVE ? &in6addr_any : &in6addr_loopback;
      local[num_local++] = ipv6_address(a->s6_addr);
    }
  } else if (inet_pton(AF_INET, host, &a4) == 1) {
    if (family == AF_INET6)
      return EAI_NONAME;
    local[num_local++] = ipv4_address((const uint8_t *)&a4.s_addr);
  } else if (inet_pton(AF_INET6, host, &a6) == 1) {
    if (family == AF_INET)
      return EAI_NONAME;
    local[num_local++] = ipv6_address(a6.s6_addr);
  } else if (flags & AI_NUMERICHOST) {
    return EAI_NONAME;
  }

  struct addrinfo *result = NULL;
  defer freeaddrinfo(result);
  struct addrinfo *current = NULL;

  if (num_local > 0) {
    for (size_t i = 0; i < num_local; i++) {
      if (protocol & SERVICE_PROTOCOL_TCP &&
          !add_addr(local[i], htons(port), SOCK_STREAM, hint, &current,
                    &result))
        return EAI_MEMORY;
      if (protocol & SERVICE_PROTOCOL_UDP &&
          !add_addr(local[i], htons(port), SOCK_DGRAM, hint, &current, &result))
        return EAI_MEMORY;
    }
  } else {
    wasi_string_t name = {.ptr = (uint8_t *)host, .len = strlen(host)};
    ip_name_lookup_error_code_t error;
#ifdef __wasip2__
    ip_name_lookup_own_resolve_address_stream_t stream;
    if (!ip_name_lookup_resolve_addresses(
            __wasi_sockets_utils__borrow_network(), &name, &stream, &error))
      return map_error(&error);
    defer ip_name_lookup_resolve_address_stream_drop_own(stream);
    ip_name_lookup_borrow_resolve_address_stream_t stream_borrow =
        ip_name_lookup_borrow_resolve_address_stream(stream);
    poll_own_pollable_t pollable;
    pollable.__handle = 0;
    defer {
      if (pollable.__handle != 0)
        poll_pollable_drop_own(pollable);
    }
#else
    ip_name_lookup_list_ip_address_t addresses;
    if (!ip_name_lookup_resolve_addresses(&name, &addresses, &error))
      return map_error(&error);
    size_t next = 0;
#endif

    while (1) {
#ifdef __wasip2__
      ip_name_lookup_option_ip_address_t maybe_address;
      if (!ip_name_lookup_method_resolve_address_stream_resolve_next_address(
              stream_borrow, &maybe_address, &error)) {
        if (error != NETWORK_ERROR_CODE_WOULD_BLOCK)
          return map_error(&error);

        if (pollable.__handle == 0) {
          pollable = ip_name_lookup_method_resolve_address_stream_subscribe(
              stream_borrow);
        }
        poll_method_pollable_block(poll_borrow_pollable(pollable));
        continue;
      }
      if (!maybe_address.is_some)
        break;
      sockets_ip_address_t address = maybe_address.val;

#else
      if (next == addresses.len)
        break;
      sockets_ip_address_t address = addresses.ptr[next++];
#endif
      if (protocol & SERVICE_PROTOCOL_TCP &&
          !add_addr(address, htons(port), SOCK_STREAM, hint, &current, &result))
        return EAI_MEMORY;
      if (protocol & SERVICE_PROTOCOL_UDP &&
          !add_addr(address, htons(port), SOCK_DGRAM, hint, &current, &result))
        return EAI_MEMORY;
    }

#ifndef __wasip2__
    ip_name_lookup_list_ip_address_free(&addresses);
#endif
  }

  if (result == NULL)
    return EAI_NONAME;

  // If a canonical name is asked for just give what was passed in.
  if (result != NULL && host != NULL && flags & AI_CANONNAME) {
    result->ai_canonname = strdup(host);
    if (result->ai_canonname == NULL)
      return EAI_MEMORY;
  }

  *res = result;
  result = NULL;
  return 0;
}

void freeaddrinfo(struct addrinfo *p) {
  while (p) {
    struct addrinfo *next = p->ai_next;
    free(p->ai_canonname);
    free(p->ai_addr);
    free(p);
    p = next;
  }
}

int getnameinfo(const struct sockaddr *restrict sa, socklen_t salen,
                char *restrict host, socklen_t hostlen, char *restrict serv,
                socklen_t servlen, int flags) {
  if (flags & ~(NI_NUMERICHOST | NI_NUMERICSERV | NI_NOFQDN | NI_NAMEREQD |
                NI_DGRAM | NI_NUMERICSCOPE))
    return EAI_BADFLAGS;
  if (sa == NULL || salen < sizeof(sa->sa_family))
    return EAI_FAMILY;

  const void *address;
  unsigned port, scope = 0;
  if (sa->sa_family == AF_INET) {
    if (salen < sizeof(struct sockaddr_in))
      return EAI_FAMILY;
    const struct sockaddr_in *addr4 = (const void *)sa;
    address = &addr4->sin_addr;
    port = ntohs(addr4->sin_port);
  } else if (sa->sa_family == AF_INET6) {
    if (salen < sizeof(struct sockaddr_in6))
      return EAI_FAMILY;
    const struct sockaddr_in6 *addr6 = (const void *)sa;
    address = &addr6->sin6_addr;
    port = ntohs(addr6->sin6_port);
    scope = addr6->sin6_scope_id;
  } else {
    return EAI_FAMILY;
  }

  bool want_host = host != NULL && hostlen != 0;
  bool want_serv = serv != NULL && servlen != 0;
  if (!want_host && !want_serv)
    return EAI_NONAME;
  // WASI has no reverse DNS lookup, so a required hostname is unavailable.
  if (want_host && (flags & NI_NAMEREQD))
    return EAI_NONAME;

  // Allow an IPv6 address followed by a decimal uint32_t scope ID.
  char node[INET6_ADDRSTRLEN + sizeof("%4294967295")];
  size_t node_len = 0, service_len = 0;
  if (want_host) {
    // Without reverse DNS, use the numeric fallback even without
    // NI_NUMERICHOST.
    if (inet_ntop(sa->sa_family, address, node, sizeof(node)) == NULL)
      return EAI_SYSTEM;
    node_len = strlen(node);
    if (scope != 0) {
      // Scopes are always numeric, as requested by musl's NI_NUMERICSCOPE.
      int len =
          snprintf(node + node_len, sizeof(node) - node_len, "%%%u", scope);
      if (len < 0)
        return EAI_SYSTEM;
      if ((size_t)len >= sizeof(node) - node_len)
        return EAI_OVERFLOW;
      node_len += len;
    }
    if (node_len >= hostlen)
      return EAI_OVERFLOW;
  }
  char number[sizeof("65535")];
  const char *service = number;
  if (want_serv) {
    const service_entry_t *entry =
        (flags & NI_NUMERICSERV) ? NULL
                                 : __wasilibc_get_service_entry_by_port(port);
    int protocol =
        (flags & NI_DGRAM) ? SERVICE_PROTOCOL_UDP : SERVICE_PROTOCOL_TCP;
    if (entry != NULL && (entry->protocol & protocol))
      service = entry->s_name;
    else {
      int len = snprintf(number, sizeof(number), "%u", port);
      if (len < 0)
        return EAI_SYSTEM;
      if ((size_t)len >= sizeof(number))
        return EAI_OVERFLOW;
    }
    service_len = strlen(service);
    if (service_len >= servlen)
      return EAI_OVERFLOW;
  }

  if (want_host)
    memcpy(host, node, node_len + 1);
  if (want_serv)
    memcpy(serv, service, service_len + 1);
  return 0;
}

struct hostent *gethostbyname(const char *name) {
  (void)name;
  // TODO wasi-sockets
  return NULL;
}

struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type) {
  (void)addr;
  (void)len;
  (void)type;
  // TODO wasi-sockets
  return NULL;
}

const char *hstrerror(int err) {
  (void)err;
  // TODO wasi-sockets
  return "hstrerror: TODO";
}

struct servent *getservbyname(const char *name, const char *proto) {
  const service_entry_t *entry = __wasilibc_get_service_entry_by_name(name);
  if (!set_global_serv_entry(entry, proto)) {
    return NULL;
  }
  return &global_serv;
}

struct servent *getservbyport(int port, const char *proto) {
  const service_entry_t *entry =
      __wasilibc_get_service_entry_by_port(htons(port));
  if (!set_global_serv_entry(entry, proto)) {
    return NULL;
  }
  return &global_serv;
}

struct protoent *getprotobyname(const char *name) {
  (void)name;
  // TODO wasi-sockets
  return NULL;
}
