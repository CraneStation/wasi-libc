#ifndef WASI_TCP_H
#define WASI_TCP_H

#include <wasi/version.h>

#if !defined(__wasip1__) && !defined(__webp2__)

#include "lock.h"
#include <sys/socket.h>
#include <wasi/sockets_utils.h>

// Normalize names on WASIp2 to the WASIp3-based names
#ifdef __wasip2__
typedef network_error_code_t sockets_error_code_t;
typedef tcp_own_tcp_socket_t sockets_own_tcp_socket_t;
typedef tcp_borrow_tcp_socket_t sockets_borrow_tcp_socket_t;
typedef network_ip_address_family_t sockets_ip_address_family_t;
#define sockets_borrow_tcp_socket tcp_borrow_tcp_socket
#endif // __wasip2__

typedef struct {
  int dummy;
} tcp_socket_state_unbound_t;
typedef struct {
  int dummy;
} tcp_socket_state_bound_t;
typedef struct {
#ifdef __wasip2__
  int dummy;
#else
  // Arguments, results, and subtask connected to `[method]tcp-socket.connect`
  // while it's in-progress. Note that `subtask` may be 0 in which case it means
  // that `result` is valid as it's been filled in.
  sockets_method_tcp_socket_connect_args_t args;
  sockets_result_void_error_code_t result;
  wasip3_subtask_t subtask;
  /// Whether `subtask` is currently registered in some `poll`'s waitable-set.
  /// Subtasks can be in at most one waitable-set at a time, so while this is
  /// set no other `poll` can register this socket.
  bool polling;
#endif
} tcp_socket_state_connecting_t;

/// The `stream` in `tcp_socket_state_listening_t` is finished and should not
/// be read again.
#define TCP_LISTENING_DONE (1 << 0)
/// There is an active read on `stream` which has yet to complete.
#define TCP_LISTENING_ACCEPTING (1 << 1)
/// The `accept_result` field is valid and ready to be processed.
#define TCP_LISTENING_ACCEPT_READY (1 << 2)
/// Whether or not a thread is blocked in `accept` on this socket meaning that
/// the stream is not suitable for any other operations.
#define TCP_LISTENING_BLOCKING (1 << 3)

typedef struct {
#ifdef __wasip2__
  int dummy;
#else
  /// Queue for concurrent blocking calls to accept to wait on.
  DECLARE_STRONG_LOCK(blocking_lock);
  // The `stream<tcp-socket>` that this is reading to receive accepted sockets.
  sockets_stream_own_tcp_socket_t stream;
  /// In-flight result of the read of `stream`.
  sockets_own_tcp_socket_t accept_result;
  /// Bitset of `TCP_LISTENING_*`.
  uint32_t flags;
#endif
} tcp_socket_state_listening_t;

// Pollables here are lazily initialized on-demand.
typedef struct {
#ifdef __wasip2__
  streams_own_input_stream_t input;
  poll_own_pollable_t input_pollable;
  streams_own_output_stream_t output;
  poll_own_pollable_t output_pollable;
#else
  // The bytes that are being received on this socket in addition to the future
  // of the result to read when the socket hits EOF to see if there's an error.
  wasip3_io_state_t receive;
  sockets_future_result_void_error_code_t receive_result;

  // The stream to write to in order to send out bytes, along with the `future`
  // that communicates the result of the send.
  wasip3_io_state_t send;
  sockets_future_result_void_error_code_t send_result;
#endif
} tcp_socket_state_connected_t;

typedef struct {
  sockets_error_code_t error_code;
} tcp_socket_state_connect_failed_t;

// This is a tagged union. When adding/removing/renaming cases, be sure to keep
// the tag and union definitions in sync.
typedef struct {
  enum {
    TCP_SOCKET_STATE_UNBOUND,
    TCP_SOCKET_STATE_BOUND,
    TCP_SOCKET_STATE_CONNECTING,
    TCP_SOCKET_STATE_CONNECTED,
    TCP_SOCKET_STATE_CONNECT_FAILED,
    TCP_SOCKET_STATE_LISTENING,
  } tag;
  union {
    tcp_socket_state_unbound_t unbound;
    tcp_socket_state_bound_t bound;
    tcp_socket_state_connecting_t connecting;
    tcp_socket_state_connected_t connected;
    tcp_socket_state_connect_failed_t connect_failed;
    tcp_socket_state_listening_t listening;
  };
} tcp_socket_state_t;

typedef struct {
  descriptor_refcnt_t refcnt;
  DECLARE_STRONG_LOCK(lock);
  sockets_own_tcp_socket_t socket;
  tcp_socket_state_t state;
#ifdef __wasip2__
  // This pollable is lazily initialized on-demand.
  poll_own_pollable_t socket_pollable;
#endif
  bool blocking;
  bool fake_nodelay;
  bool fake_reuseaddr;
  sockets_ip_address_family_t family;
  monotonic_clock_duration_t send_timeout;
  monotonic_clock_duration_t recv_timeout;
} tcp_socket_t;

int __wasilibc_tcp_getsockopt(void *data, int level, int optname,
                              void *restrict optval,
                              socklen_t *restrict optlen);
int __wasilibc_tcp_setsockopt(void *data, int level, int optname,
                              const void *optval, socklen_t optlen);

/// Adds the provided TCP socket to the descriptor table, returning the
/// corresponding file descriptor.
int __wasilibc_add_tcp_socket(sockets_own_tcp_socket_t socket,
                              sockets_ip_address_family_t family,
                              bool blocking);

#endif // !__wasip1__ && !__webp2__

#endif // WASI_TCP_H
