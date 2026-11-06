#include <wasi/api.h>

#ifndef __wasip1__

#include <errno.h>
#include <limits.h>
#include <netinet/tcp.h>
#include <stddefer.h>
#include <stdlib.h>
#include <string.h>
#include <wasi/descriptor_table.h>
#include <wasi/file_utils.h>
#include <wasi/sockets_utils.h>
#include <wasi/tcp.h>
#include <wasi/wasip3_block.h>

// Normalize names on WASIp2 to the WASIp3-based names
#ifdef __wasip2__

#include <wasi/wasip2.h>

#define sockets_method_tcp_socket_get_is_listening                             \
  tcp_method_tcp_socket_is_listening
#define sockets_method_tcp_socket_get_keep_alive_enabled                       \
  tcp_method_tcp_socket_keep_alive_enabled
#define sockets_method_tcp_socket_get_receive_buffer_size                      \
  tcp_method_tcp_socket_receive_buffer_size
#define sockets_method_tcp_socket_get_send_buffer_size                         \
  tcp_method_tcp_socket_send_buffer_size
#define sockets_method_tcp_socket_set_keep_alive_enabled                       \
  tcp_method_tcp_socket_set_keep_alive_enabled
#define sockets_method_tcp_socket_set_receive_buffer_size                      \
  tcp_method_tcp_socket_set_receive_buffer_size
#define sockets_method_tcp_socket_set_send_buffer_size                         \
  tcp_method_tcp_socket_set_send_buffer_size
#define sockets_method_tcp_socket_get_hop_limit tcp_method_tcp_socket_hop_limit
#define sockets_method_tcp_socket_set_hop_limit                                \
  tcp_method_tcp_socket_set_hop_limit
#define sockets_method_tcp_socket_get_keep_alive_idle_time                     \
  tcp_method_tcp_socket_keep_alive_idle_time
#define sockets_method_tcp_socket_get_keep_alive_interval                      \
  tcp_method_tcp_socket_keep_alive_interval
#define sockets_method_tcp_socket_get_keep_alive_count                         \
  tcp_method_tcp_socket_keep_alive_count
#define sockets_method_tcp_socket_set_keep_alive_idle_time                     \
  tcp_method_tcp_socket_set_keep_alive_idle_time
#define sockets_method_tcp_socket_set_keep_alive_interval                      \
  tcp_method_tcp_socket_set_keep_alive_interval
#define sockets_method_tcp_socket_set_keep_alive_count                         \
  tcp_method_tcp_socket_set_keep_alive_count
#define sockets_method_tcp_socket_get_remote_address                           \
  tcp_method_tcp_socket_remote_address
#define sockets_method_tcp_socket_get_local_address                            \
  tcp_method_tcp_socket_local_address
#define sockets_method_tcp_socket_set_listen_backlog_size                      \
  tcp_method_tcp_socket_set_listen_backlog_size

#define sockets_borrow_tcp_socket tcp_borrow_tcp_socket
#define sockets_borrow_tcp_socket_t tcp_borrow_tcp_socket_t
#define sockets_own_tcp_socket_t tcp_own_tcp_socket_t
#define sockets_tcp_socket_drop_own tcp_tcp_socket_drop_own

typedef tcp_duration_t sockets_duration_t;
#endif // __wasip2__

static const uint64_t NS_PER_S = 1000000000;

static descriptor_vtable_t tcp_vtable;

static int tcp_add(sockets_own_tcp_socket_t socket,
                   sockets_ip_address_family_t family, bool blocking,
                   tcp_socket_t **out) {
  tcp_socket_t *tcp = calloc(1, sizeof(tcp_socket_t));
  if (!tcp) {
    sockets_tcp_socket_drop_own(socket);
    errno = ENOMEM;
    return -1;
  }
  tcp->state.tag = TCP_SOCKET_STATE_UNBOUND;
  tcp->socket = socket;
  tcp->family = family;
  tcp->blocking = blocking;

  descriptor_table_entry_t entry;
  entry.vtable = &tcp_vtable;
  entry.data = &tcp->refcnt;
  if (out)
    *out = tcp;
  return descriptor_table_insert(entry);
}

int __wasilibc_add_tcp_socket(sockets_own_tcp_socket_t socket,
                              sockets_ip_address_family_t family,
                              bool blocking) {
  return tcp_add(socket, family, blocking, NULL);
}

#ifdef __wasip3__
static void wasip3_tcp_accept_finish(tcp_socket_state_listening_t *state,
                                     wasip3_waitable_status_t status);
#endif

static void tcp_free(void *data) {
  tcp_socket_t *tcp = (tcp_socket_t *)data;
  STRONG_ASSERT_EMPTY(tcp->lock);

  switch (tcp->state.tag) {
#ifdef __wasip3__
  case TCP_SOCKET_STATE_CONNECTING: {
    tcp_socket_state_connecting_t *state = &tcp->state.connecting;
    if (state->subtask != 0) {
      wasip3_subtask_cancel(state->subtask);
      wasip3_subtask_drop(state->subtask);
    }
    break;
  }

  case TCP_SOCKET_STATE_LISTENING: {
    tcp_socket_state_listening_t *state = &tcp->state.listening;
    STRONG_ASSERT_EMPTY(state->blocking_lock);
    if (state->flags & TCP_LISTENING_ACCEPTING)
      wasip3_tcp_accept_finish(
          state, sockets_stream_own_tcp_socket_cancel_read(state->stream));
    if (state->flags & TCP_LISTENING_ACCEPT_READY)
      sockets_tcp_socket_drop_own(state->accept_result);
    if (state->stream != 0)
      sockets_stream_own_tcp_socket_drop_readable(state->stream);
    break;
  }
#endif

  case TCP_SOCKET_STATE_CONNECTED: {
    tcp_socket_state_connected_t *state = &tcp->state.connected;

#ifdef __wasip2__
    if (state->input_pollable.__handle != 0)
      poll_pollable_drop_own(state->input_pollable);
    if (state->output_pollable.__handle != 0)
      poll_pollable_drop_own(state->output_pollable);
    streams_input_stream_drop_own(state->input);
    streams_output_stream_drop_own(state->output);
#else
    wasip3_read_state_close(&state->receive);
    if (state->receive_result != 0)
      sockets_future_result_void_error_code_drop_readable(
          state->receive_result);
    STRONG_LOCK(tcp->lock);
    wasip3_write_state_close(&state->send);
    STRONG_UNLOCK(tcp->lock);
    if (state->send_result != 0)
      sockets_future_result_void_error_code_drop_readable(state->send_result);
#endif
    break;
  }

  default:
    break;
  }

#ifdef __wasip2__
  if (tcp->socket_pollable.__handle != 0)
    poll_pollable_drop_own(tcp->socket_pollable);
#endif // __wasip2__
  sockets_tcp_socket_drop_own(tcp->socket);

  free(tcp);
}

#ifndef __wasip2__
static int tcp_read_eof(void *data) {
  tcp_socket_t *tcp = (tcp_socket_t *)data;
  STRONG_ASSERT_HELD(tcp->lock);

  assert(tcp->state.tag == TCP_SOCKET_STATE_CONNECTED);
  tcp_socket_state_connected_t *state = &tcp->state.connected;

  if (state->receive_result) {
    sockets_result_void_error_code_t result;
    __wasilibc_future_block_on(sockets_future_result_void_error_code_read(
                                   state->receive_result, &result),
                               state->receive_result);
    sockets_future_result_void_error_code_drop_readable(state->receive_result);
    state->receive_result = 0;
    if (result.is_err)
      return __wasilibc_socket_error_to_errno(&result.val.err);
  }
  return 0;
}
#endif

static int tcp_get_read_stream(void *data, wasi_read_t *read) {
  tcp_socket_t *tcp = (tcp_socket_t *)data;
  STRONG_LOCK(tcp->lock);
  // .. intentionally don't unlock `tcp->lock` as this function lets the
  // caller do that (see `descriptor_table.h` for details of this callback).

  if (tcp->state.tag != TCP_SOCKET_STATE_CONNECTED) {
    STRONG_UNLOCK(tcp->lock);
    errno = ENOTCONN;
    return -1;
  }
  tcp_socket_state_connected_t *state = &tcp->state.connected;
#ifdef __wasip2__
  read->input = streams_borrow_input_stream(state->input);
  read->pollable = &state->input_pollable;
  read->eof = NULL;
#else
  read->state = &state->receive;
  read->eof = tcp_read_eof;
  read->eof_data = data;
#endif
  read->offset = NULL;
  read->timeout = tcp->recv_timeout;
  read->blocking = tcp->blocking;
  return 0;
}

#ifndef __wasip2__
static int tcp_write_eof(void *data) {
  tcp_socket_t *tcp = (tcp_socket_t *)data;
  STRONG_ASSERT_HELD(tcp->lock);

  assert(tcp->state.tag == TCP_SOCKET_STATE_CONNECTED);
  tcp_socket_state_connected_t *state = &tcp->state.connected;

  if (state->send_result) {
    sockets_result_void_error_code_t result;
    __wasilibc_future_block_on(
        sockets_future_result_void_error_code_read(state->send_result, &result),
        state->send_result);
    sockets_future_result_void_error_code_drop_readable(state->send_result);
    state->send_result = 0;
    if (result.is_err)
      return __wasilibc_socket_error_to_errno(&result.val.err);
  }
  errno = EPIPE;
  return -1;
}
#endif

static int tcp_get_write_stream(void *data, wasi_write_t *write) {
  tcp_socket_t *tcp = (tcp_socket_t *)data;
  STRONG_LOCK(tcp->lock);
  // .. intentionally don't unlock `tcp->lock` as this function lets the
  // caller do that (see `descriptor_table.h` for details of this callback).

  if (tcp->state.tag != TCP_SOCKET_STATE_CONNECTED) {
    STRONG_UNLOCK(tcp->lock);
    errno = ENOTCONN;
    return -1;
  }
  tcp_socket_state_connected_t *state = &tcp->state.connected;
#ifdef __wasip2__
  write->output = streams_borrow_output_stream(state->output);
  write->pollable = &state->output_pollable;
#else
  write->state = &state->send;
  write->eof = tcp_write_eof;
  write->eof_data = data;
#endif
  write->offset = NULL;
  write->timeout = tcp->send_timeout;
  write->blocking = tcp->blocking;
  return 0;
}

static int tcp_set_blocking(void *data, bool blocking) {
  tcp_socket_t *tcp = (tcp_socket_t *)data;
  STRONG_LOCK(tcp->lock);
  defer STRONG_UNLOCK(tcp->lock);

  tcp->blocking = blocking;
  return 0;
}

static int tcp_fstat(void *data, struct stat *buf) {
  (void)data;
  memset(buf, 0, sizeof(struct stat));
  buf->st_mode = S_IFSOCK;
  return 0;
}

#ifdef __wasip2__
static poll_borrow_pollable_t tcp_pollable(tcp_socket_t *socket) {
  if (socket->socket_pollable.__handle == 0) {
    sockets_borrow_tcp_socket_t socket_borrow =
        sockets_borrow_tcp_socket(socket->socket);
    socket->socket_pollable = tcp_method_tcp_socket_subscribe(socket_borrow);
  }
  return poll_borrow_pollable(socket->socket_pollable);
}

static int tcp_handle_error(tcp_socket_t *socket, sockets_error_code_t *error) {
  if (*error == NETWORK_ERROR_CODE_WOULD_BLOCK && socket->blocking) {
    poll_method_pollable_block(tcp_pollable(socket));
  } else {
    return __wasilibc_socket_error_to_errno(error);
  }

  return 0;
}

#else

// Setup the `TCP_SOCKET_STATE_CONNECTED` fields for a wasip3-connected socket.
static void tcp_setup_connected_state_wasip3(tcp_socket_t *socket) {
  STRONG_ASSERT_HELD(socket->lock);

  sockets_borrow_tcp_socket_t socket_borrow =
      sockets_borrow_tcp_socket(socket->socket);

  socket->state.tag = TCP_SOCKET_STATE_CONNECTED;
  tcp_socket_state_connected_t *state = &socket->state.connected;

  sockets_tuple2_stream_u8_future_result_void_error_code_t receive_result;
  sockets_method_tcp_socket_receive(socket_borrow, &receive_result);
  wasip3_io_state_init(&state->receive, receive_result.f0, socket->lock);
  state->receive_result = receive_result.f1;

  sockets_stream_u8_writer_t send;
  sockets_stream_u8_t reader = sockets_stream_u8_new(&send);
  wasip3_io_state_init(&state->send, send, socket->lock);
  state->send_result = sockets_method_tcp_socket_send(socket_borrow, reader);
}
#endif

#ifndef __wasip2__
static void wasip3_tcp_accept_finish(tcp_socket_state_listening_t *state,
                                     wasip3_waitable_status_t status) {
  assert(state->flags & TCP_LISTENING_ACCEPTING);
  assert(!(state->flags & TCP_LISTENING_DONE));
  assert(!(state->flags & TCP_LISTENING_ACCEPT_READY));
  state->flags &= ~TCP_LISTENING_ACCEPTING;

  switch (WASIP3_WAITABLE_STATE(status)) {
  case WASIP3_WAITABLE_DROPPED:
    state->flags |= TCP_LISTENING_DONE;
    break;
  case WASIP3_WAITABLE_COMPLETED:
  case WASIP3_WAITABLE_CANCELLED:
    break;
  default:
    abort();
  }
  if (WASIP3_WAITABLE_COUNT(status) > 0)
    state->flags |= TCP_LISTENING_ACCEPT_READY;
}

static void wasip3_tcp_accept_finish_event(tcp_socket_state_listening_t *state,
                                           wasip3_event_t *event) {
  assert(event->event == WASIP3_EVENT_STREAM_READ);
  assert(event->waitable == state->stream);
  wasip3_tcp_accept_finish(state, event->code);
}

/// Kicks off any necessary work to accept a TCP socket.
///
/// Returns `true` if this actually performed a stream read, and `false`
/// otherwise. Note that if `false` is returned there may still be an active
/// read or a pending value.
static bool wasip3_tcp_accept_start(tcp_socket_state_listening_t *state) {
  // Kick off an accept while we're (a) not done, (b) there's not already an
  // active accept, and (c) a previous accept hasn't finished.
  while (!(state->flags & TCP_LISTENING_DONE) &&
         !(state->flags & TCP_LISTENING_ACCEPTING) &&
         !(state->flags & TCP_LISTENING_ACCEPT_READY)) {
    wasip3_waitable_status_t status = sockets_stream_own_tcp_socket_read(
        state->stream, &state->accept_result, 1);
    state->flags |= TCP_LISTENING_ACCEPTING;
    if (status == WASIP3_WAITABLE_STATUS_BLOCKED) {
      return true;
    }
    wasip3_tcp_accept_finish(state, status);
  }
  return false;
}

/// This function is similar to `wasip3_io_sync` in `file_utils.c`
static int wasip3_accept_sync(tcp_socket_t *socket, bool blocking) {
  STRONG_ASSERT_HELD(socket->lock);
  assert(socket->state.tag == TCP_SOCKET_STATE_LISTENING);
  tcp_socket_state_listening_t *state = &socket->state.listening;

  while (state->flags & TCP_LISTENING_BLOCKING) {
    if (!blocking) {
      errno = EOPNOTSUPP;
      return -1;
    }
#ifdef _REENTRANT
    STRONG_UNLOCK(socket->lock);

    // Bounce on the lock to wait for pending blocking I/O to complete.
    STRONG_LOCK(state->blocking_lock);
    STRONG_UNLOCK(state->blocking_lock);

    STRONG_LOCK(socket->lock);
#else
    // should not be possible to hit if threads are disabled
    __builtin_trap();
#endif
  }

  return 0;
}

/// This function is similar to `wasip3_enter_blocking_operation` in
/// `file_utils.c`
static void wasip3_accept_enter_blocking(tcp_socket_t *socket) {
  STRONG_ASSERT_HELD(socket->lock);
  assert(socket->state.tag == TCP_SOCKET_STATE_LISTENING);
  tcp_socket_state_listening_t *state = &socket->state.listening;

  assert(!(state->flags & TCP_LISTENING_BLOCKING));
  state->flags |= TCP_LISTENING_BLOCKING;
  STRONG_LOCK(state->blocking_lock);
}

/// This function is similar to `wasip3_exit_blocking_operation` in
/// `file_utils.c`
static void wasip3_accept_exit_blocking(tcp_socket_t *socket) {
  assert(socket->state.tag == TCP_SOCKET_STATE_LISTENING);
  tcp_socket_state_listening_t *state = &socket->state.listening;

  STRONG_ASSERT_HELD(state->blocking_lock);
  STRONG_ASSERT_HELD(socket->lock);
  STRONG_UNLOCK(state->blocking_lock);
  assert(state->flags & TCP_LISTENING_BLOCKING);
  state->flags &= ~TCP_LISTENING_BLOCKING;
}
#endif // !__wasip2__

static int tcp_accept4(void *data, struct sockaddr *addr, socklen_t *addrlen,
                       int flags) {
  tcp_socket_t *socket = (tcp_socket_t *)data;
  STRONG_LOCK(socket->lock);
  defer STRONG_UNLOCK(socket->lock);

  output_sockaddr_t output_addr;
  if (__wasilibc_sockaddr_validate(socket->family, addr, addrlen,
                                   &output_addr) < 0) {
    return -1;
  }

  if (socket->state.tag != TCP_SOCKET_STATE_LISTENING) {
    errno = EINVAL;
    return -1;
  }

  tcp_socket_t *client_socket;
  sockets_error_code_t error;

#ifdef __wasip2__
  sockets_borrow_tcp_socket_t socket_borrow =
      sockets_borrow_tcp_socket(socket->socket);
  tcp_tuple3_own_tcp_socket_own_input_stream_own_output_stream_t client_and_io;
  while (!tcp_method_tcp_socket_accept(socket_borrow, &client_and_io, &error)) {
    if (tcp_handle_error(socket, &error) < 0)
      return -1;
  }

  sockets_own_tcp_socket_t client = client_and_io.f0;
  streams_own_input_stream_t input = client_and_io.f1;
  streams_own_output_stream_t output = client_and_io.f2;

  int client_fd = tcp_add(client, socket->family, (flags & SOCK_NONBLOCK) == 0,
                          &client_socket);
  if (client_fd < 0) {
    streams_input_stream_drop_own(input);
    streams_output_stream_drop_own(output);
    return -1;
  }

  client_socket->state.tag = TCP_SOCKET_STATE_CONNECTED;
  memset(&client_socket->state.connected, 0,
         sizeof(client_socket->state.connected));
  client_socket->state.connected.input = input;
  client_socket->state.connected.output = output;
#else
  tcp_socket_state_listening_t *state = &socket->state.listening;

  if (wasip3_accept_sync(socket, socket->blocking) < 0)
    return -1;

  // Turn this loop until a socket is fully accepted and ready to get
  // processed.
  while (!(state->flags & TCP_LISTENING_ACCEPT_READY)) {
    bool started_work = wasip3_tcp_accept_start(state);

    // If the accept immediately completed we're good to go.
    if (state->flags & TCP_LISTENING_ACCEPT_READY)
      break;

    // It's not clear what the correct error code to return here is, if any,
    // since in theory sockets being accepted are infinite until the socket is
    // closed. Handle this with at least some error for now.
    if (state->flags & TCP_LISTENING_DONE) {
      errno = ENOTSUP;
      return -1;
    }

    assert(state->flags & TCP_LISTENING_ACCEPTING);

    // Either block waiting for this to complete in blocking mode or poll to
    // see what happened in non-blocking mode. As a minor optimization if
    // an accept was kicked off above and it didn't finish then don't re-poll
    // here and just bail out immediately.
    wasip3_event_t event;
    if (socket->blocking) {
      wasip3_accept_enter_blocking(socket);
      STRONG_UNLOCK(socket->lock);
      __wasilibc_waitable_block_on(state->stream, &event, 0);
      STRONG_LOCK(socket->lock);
      wasip3_accept_exit_blocking(socket);
    } else {
      if (!started_work)
        __wasilibc_poll_waitable(state->stream, &event);
      if (started_work || event.event == WASIP3_EVENT_NONE) {
        errno = EWOULDBLOCK;
        return -1;
      }
    }

    // Update our own internal state with the result of the accept, and then
    // turn the loop again.
    wasip3_tcp_accept_finish_event(state, &event);
  }

  assert(!(state->flags & TCP_LISTENING_ACCEPTING));

  int client_fd = tcp_add(state->accept_result, socket->family,
                          (flags & SOCK_NONBLOCK) == 0, &client_socket);
  state->accept_result.__handle = 0;
  state->flags &= ~TCP_LISTENING_ACCEPT_READY;
  if (client_fd < 0)
    return -1;
  STRONG_LOCK(client_socket->lock);
  defer STRONG_UNLOCK(client_socket->lock);
  tcp_setup_connected_state_wasip3(client_socket);
#endif

  sockets_borrow_tcp_socket_t client_borrow =
      sockets_borrow_tcp_socket(client_socket->socket);
  if (output_addr.tag != OUTPUT_SOCKADDR_NULL) {
    sockets_ip_socket_address_t remote_address;
    if (!sockets_method_tcp_socket_get_remote_address(
            client_borrow, &remote_address, &error)) {
      // TODO wasi-sockets: How to recover from this in a POSIX compatible way?
      abort();
    }

    __wasilibc_wasi_to_sockaddr(remote_address, &output_addr);
  }
  client_socket->fake_nodelay = socket->fake_nodelay;
  return client_fd;
}

static int tcp_do_bind(tcp_socket_t *socket,
                       sockets_ip_socket_address_t *address) {
  STRONG_ASSERT_HELD(socket->lock);
  if (socket->state.tag != TCP_SOCKET_STATE_UNBOUND) {
    errno = EINVAL;
    return -1;
  }

  sockets_error_code_t error;
  sockets_borrow_tcp_socket_t socket_borrow =
      sockets_borrow_tcp_socket(socket->socket);

#if defined(__wasip2__)
  network_borrow_network_t network_borrow =
      __wasi_sockets_utils__borrow_network();

  if (!tcp_method_tcp_socket_start_bind(socket_borrow, network_borrow, address,
                                        &error)) {
    return __wasilibc_socket_error_to_errno(&error);
  }

  // Bind has successfully started. Attempt to finish it:
  while (!tcp_method_tcp_socket_finish_bind(socket_borrow, &error)) {
    if (tcp_handle_error(socket, &error) < 0)
      return -1;
  }
#elif defined(__wasip3__)
  if (!sockets_method_tcp_socket_bind(socket_borrow, address, &error)) {
    return __wasilibc_socket_error_to_errno(&error);
  }
#endif

  // Bind successful.
  socket->state.tag = TCP_SOCKET_STATE_BOUND;
  return 0;
}

static int tcp_bind(void *data, const struct sockaddr *addr,
                    socklen_t addrlen) {
  tcp_socket_t *socket = (tcp_socket_t *)data;
  STRONG_LOCK(socket->lock);
  defer STRONG_UNLOCK(socket->lock);

  sockets_ip_socket_address_t local_address;
  if (__wasilibc_sockaddr_to_wasi(socket->family, addr, addrlen,
                                  &local_address) < 0)
    return -1;
  return tcp_do_bind(socket, &local_address);
}

#ifndef __wasip2__
static void tcp_connect_finish(tcp_socket_t *socket) {
  STRONG_ASSERT_HELD(socket->lock);

  assert(socket->state.tag == TCP_SOCKET_STATE_CONNECTING);
  tcp_socket_state_connecting_t *conn = &socket->state.connecting;

  // The connect subtask has completed at this point, so check to see what the
  // result was.
  assert(conn->subtask == 0);
  if (conn->result.is_err) {
    sockets_error_code_t error = conn->result.val.err;
    socket->state.tag = TCP_SOCKET_STATE_CONNECT_FAILED;
    socket->state.connect_failed.error_code = error;
  } else {
    tcp_setup_connected_state_wasip3(socket);
  }
}
#endif // !__wasip2__

static int tcp_connect(void *data, const struct sockaddr *addr,
                       socklen_t addrlen) {
  tcp_socket_t *socket = (tcp_socket_t *)data;
  STRONG_LOCK(socket->lock);
  defer STRONG_UNLOCK(socket->lock);

  sockets_ip_socket_address_t remote_address;
  if (__wasilibc_sockaddr_to_wasi(socket->family, addr, addrlen,
                                  &remote_address) < 0)
    return -1;

  switch (socket->state.tag) {
  case TCP_SOCKET_STATE_UNBOUND:
  case TCP_SOCKET_STATE_BOUND:
    // These can initiate a connect.
    break;
  case TCP_SOCKET_STATE_CONNECTING:
    errno = EALREADY;
    return -1;
  case TCP_SOCKET_STATE_CONNECTED:
    errno = EISCONN;
    return -1;
  case TCP_SOCKET_STATE_CONNECT_FAILED: // POSIX: "If connect() fails, the state
                                        // of the socket is unspecified.
                                        // Conforming applications should close
                                        // the file descriptor and create a new
                                        // socket before attempting to
                                        // reconnect."
  case TCP_SOCKET_STATE_LISTENING:
  default:
    errno = EOPNOTSUPP;
    return -1;
  }

  sockets_borrow_tcp_socket_t socket_borrow =
      sockets_borrow_tcp_socket(socket->socket);

#ifdef __wasip2__
  sockets_error_code_t error;
  network_borrow_network_t network_borrow =
      __wasi_sockets_utils__borrow_network();

  if (!tcp_method_tcp_socket_start_connect(socket_borrow, network_borrow,
                                           &remote_address, &error)) {
    return __wasilibc_socket_error_to_errno(&error);
  }

  // Connect has successfully started.
  socket->state.tag = TCP_SOCKET_STATE_CONNECTING;

  // Attempt to finish it:
  tcp_tuple2_own_input_stream_own_output_stream_t io;
  while (!tcp_method_tcp_socket_finish_connect(socket_borrow, &io, &error)) {
    if (tcp_handle_error(socket, &error) < 0) {
      if (error == NETWORK_ERROR_CODE_WOULD_BLOCK) {
        errno = EINPROGRESS;
      } else {
        socket->state.tag = TCP_SOCKET_STATE_CONNECT_FAILED;
        socket->state.connect_failed.error_code = error;
      }
      return -1;
    }
  }

  // Connect successful.
  streams_own_input_stream_t input = io.f0;
  streams_own_output_stream_t output = io.f1;

  socket->state.tag = TCP_SOCKET_STATE_CONNECTED;
  memset(&socket->state.connected, 0, sizeof(socket->state.connected));
  socket->state.connected.input = input;
  socket->state.connected.output = output;
#else
  socket->state.tag = TCP_SOCKET_STATE_CONNECTING;
  // Setup the arguments to the `connect` function as well as initializing the
  // `subtask` field of the `connecting` state to zero as we're not sure we'll
  // get a subtask just yet.
  socket->state.connecting.args.self = socket_borrow;
  socket->state.connecting.args.remote_address = remote_address;
  socket->state.connecting.subtask = 0;
  socket->state.connecting.polling = false;

  wasip3_subtask_status_t status = sockets_method_tcp_socket_connect(
      &socket->state.connecting.args, &socket->state.connecting.result);

  // If the subtask hasn't returned yet then its arguments/results are stored
  // in the `TCP_SOCKET_STATE_CONNECTING` state that we're in right now, as
  // stable addresses. Here if the socket is in blocking mode we block on the
  // result of the task, and otherwise this returns that the connect is in
  // progress and otherwise bails out.
  //
  // Note that if the socket is in blocking mode we need to block on the
  // subtask here if one is created. When doing so we need to do something
  // about our socket's lock which is otherwise currently held. The naive
  // "just drop it and re-acquire it", however, should work here. The only
  // function in this module to mutate the socket's state away from connecting
  // is via `poll`, and there's specifically a clause there which tests if
  // `subtask` is 0 and rejects polls on that socket. Otherwise the socket will
  // be entirely unusable/internal for the duration of this blocking operation,
  // so it should be safe to without any extra accounting to just drop the lock.
  if (WASIP3_SUBTASK_STATE(status) != WASIP3_SUBTASK_RETURNED) {
    wasip3_subtask_t subtask = WASIP3_SUBTASK_HANDLE(status);
    if (socket->blocking) {
      STRONG_UNLOCK(socket->lock);
      __wasilibc_subtask_block_on_and_drop(subtask);
      STRONG_LOCK(socket->lock);
      assert(socket->state.tag == TCP_SOCKET_STATE_CONNECTING);
    } else {
      socket->state.connecting.subtask = subtask;
      errno = EINPROGRESS;
      return -1;
    }
  }

  // The connect subtask has completed at this point, so check to see what the
  // result was.
  tcp_connect_finish(socket);

  if (socket->state.tag == TCP_SOCKET_STATE_CONNECT_FAILED) {
    __wasilibc_socket_error_to_errno(&socket->state.connect_failed.error_code);
    return -1;
  }
#endif

  return 0;
}

static int tcp_getsockname(void *data, struct sockaddr *addr,
                           socklen_t *addrlen) {
  tcp_socket_t *socket = (tcp_socket_t *)data;
  STRONG_LOCK(socket->lock);
  defer STRONG_UNLOCK(socket->lock);

  output_sockaddr_t output_addr;
  if (__wasilibc_sockaddr_validate(socket->family, addr, addrlen,
                                   &output_addr) < 0)
    return -1;

  if (output_addr.tag == OUTPUT_SOCKADDR_NULL) {
    errno = EINVAL;
    return -1;
  }

  switch (socket->state.tag) {
  case TCP_SOCKET_STATE_UNBOUND:
    errno = EINVAL;
    return -1;

  case TCP_SOCKET_STATE_BOUND:
  case TCP_SOCKET_STATE_CONNECTING:
  case TCP_SOCKET_STATE_CONNECT_FAILED:
  case TCP_SOCKET_STATE_LISTENING:
  case TCP_SOCKET_STATE_CONNECTED:
    // OK. Continue..
    break;

  default: /* unreachable */
    abort();
  }

  sockets_error_code_t error;
  sockets_ip_socket_address_t result;
  sockets_borrow_tcp_socket_t socket_borrow =
      sockets_borrow_tcp_socket(socket->socket);
  if (!sockets_method_tcp_socket_get_local_address(socket_borrow, &result,
                                                   &error))
    return __wasilibc_socket_error_to_errno(&error);

  __wasilibc_wasi_to_sockaddr(result, &output_addr);
  return 0;
}

static int tcp_getpeername(void *data, struct sockaddr *addr,
                           socklen_t *addrlen) {
  tcp_socket_t *socket = (tcp_socket_t *)data;
  STRONG_LOCK(socket->lock);
  defer STRONG_UNLOCK(socket->lock);

  output_sockaddr_t output_addr;
  if (__wasilibc_sockaddr_validate(socket->family, addr, addrlen,
                                   &output_addr) < 0)
    return -1;

  if (output_addr.tag == OUTPUT_SOCKADDR_NULL) {
    errno = EINVAL;
    return -1;
  }

  switch (socket->state.tag) {
  case TCP_SOCKET_STATE_UNBOUND:
  case TCP_SOCKET_STATE_BOUND:
  case TCP_SOCKET_STATE_CONNECTING:
  case TCP_SOCKET_STATE_CONNECT_FAILED:
  case TCP_SOCKET_STATE_LISTENING:
    errno = ENOTCONN;
    return -1;

  case TCP_SOCKET_STATE_CONNECTED:
    // OK. Continue..
    break;

  default: /* unreachable */
    abort();
  }

  sockets_error_code_t error;
  sockets_ip_socket_address_t result;
  sockets_borrow_tcp_socket_t socket_borrow =
      sockets_borrow_tcp_socket(socket->socket);
  if (!sockets_method_tcp_socket_get_remote_address(socket_borrow, &result,
                                                    &error))
    return __wasilibc_socket_error_to_errno(&error);

  __wasilibc_wasi_to_sockaddr(result, &output_addr);
  return 0;
}

static int tcp_listen(void *data, int backlog) {
  tcp_socket_t *socket = (tcp_socket_t *)data;
  STRONG_LOCK(socket->lock);
  defer STRONG_UNLOCK(socket->lock);

  sockets_error_code_t error;
  sockets_borrow_tcp_socket_t socket_borrow =
      sockets_borrow_tcp_socket(socket->socket);

  // POSIX says negative values behave like 0, and 0 means it can be set to an
  // implementation-defined minimum. WASI rejects 0, but POSIX doesn't, so set
  // it to an implementation-defined minimum.
  if (backlog < 1)
    backlog = 1;

  switch (socket->state.tag) {
  case TCP_SOCKET_STATE_UNBOUND: {
    // Socket is not explicitly bound by the user. We'll do it for them:

    sockets_ip_socket_address_t any;
    __wasilibc_unspecified_addr(socket->family, &any);
    if (tcp_do_bind(socket, &any) < 0)
      return -1;

    if (socket->state.tag != TCP_SOCKET_STATE_BOUND) {
      abort();
    }
    // Great! We'll continue below.
    break;
  }
  case TCP_SOCKET_STATE_BOUND:
    // Great! We'll continue below.
    break;
  case TCP_SOCKET_STATE_LISTENING:
    // We can only update the backlog size.
    break;
  case TCP_SOCKET_STATE_CONNECTING:
  case TCP_SOCKET_STATE_CONNECTED:
  case TCP_SOCKET_STATE_CONNECT_FAILED:
  default:
    errno = EINVAL;
    return -1;
  }

  if (!sockets_method_tcp_socket_set_listen_backlog_size(socket_borrow, backlog,
                                                         &error)) {
    return __wasilibc_socket_error_to_errno(&error);
  }

  if (socket->state.tag == TCP_SOCKET_STATE_LISTENING) {
    // Updating the backlog is all we had to do.
    return 0;
  }

#ifdef __wasip2__
  if (!tcp_method_tcp_socket_start_listen(socket_borrow, &error)) {
    return __wasilibc_socket_error_to_errno(&error);
  }

  // Listen has successfully started. Attempt to finish it:
  while (!tcp_method_tcp_socket_finish_listen(socket_borrow, &error)) {
    if (tcp_handle_error(socket, &error) < 0)
      return -1;
  }

  socket->state.tag = TCP_SOCKET_STATE_LISTENING;
#else
  sockets_stream_own_tcp_socket_t stream;
  if (!sockets_method_tcp_socket_listen(socket_borrow, &stream, &error))
    return __wasilibc_socket_error_to_errno(&error);

  socket->state.tag = TCP_SOCKET_STATE_LISTENING;
  memset(&socket->state.listening, 0, sizeof(socket->state.listening));
  socket->state.listening.stream = stream;
#endif

  return 0;
}

static ssize_t tcp_recvfrom(void *data, void *buffer, size_t length, int flags,
                            struct sockaddr *addr, socklen_t *addrlen) {
  // TODO wasi-sockets: flags:
  // - MSG_WAITALL: we can probably support these relatively easy.
  // - MSG_OOB: could be shimmed by always responding that no OOB data is
  // available.
  // - MSG_PEEK: could be shimmed by performing the receive into a local
  // socket-specific buffer. And on subsequent receives first check that buffer.

  const int supported_flags = MSG_DONTWAIT;
  if ((flags & supported_flags) != flags) {
    errno = EOPNOTSUPP;
    return -1;
  }

  // Mostly ignore `addr`, and fill in `addrlen` with 0 if provided since we're
  // not going to be filling in the address. This is intended to match Linux
  // behvaior at this time.
  (void)addr;
  if (addrlen != NULL)
    *addrlen = 0;

  wasi_read_t read;
  if (tcp_get_read_stream(data, &read) < 0)
    return -1;
  defer STRONG_UNLOCK(*read.state->lock);

  if ((flags & MSG_DONTWAIT) != 0)
    read.blocking = false;

  return __wasilibc_read(&read, buffer, length);
}

static ssize_t tcp_sendto(void *data, const void *buffer, size_t length,
                          int flags, const struct sockaddr *addr,
                          socklen_t addrlen) {
  const int supported_flags = MSG_DONTWAIT | MSG_NOSIGNAL;
  if ((flags & supported_flags) != flags) {
    errno = EOPNOTSUPP;
    return -1;
  }

  // POSIX says to addr/addrlen may be ignored and may return EISCONN. Match
  // Linux and do nothing.
  (void)addr;
  (void)addrlen;

  wasi_write_t write;
  if (tcp_get_write_stream(data, &write) < 0)
    return -1;
  defer STRONG_UNLOCK(*write.state->lock);

  if ((flags & MSG_DONTWAIT) != 0)
    write.blocking = false;

  if ((flags & MSG_NOSIGNAL) != 0) {
    // Ignore it. WASI has no Unix-style signals. So effectively,
    // MSG_NOSIGNAL is always the case, whether it was explicitly
    // requested or not.
  }

  return __wasilibc_write(&write, buffer, length);
}

static int tcp_shutdown(void *data, int posix_how) {
  tcp_socket_t *socket = (tcp_socket_t *)data;
  STRONG_LOCK(socket->lock);
  defer STRONG_UNLOCK(socket->lock);

  if (socket->state.tag != TCP_SOCKET_STATE_CONNECTED) {
    errno = ENOTCONN;
    return -1;
  }

#ifdef __wasip2__
  tcp_shutdown_type_t wasi_how;
  switch (posix_how) {
  case SHUT_RD:
    wasi_how = TCP_SHUTDOWN_TYPE_RECEIVE;
    break;
  case SHUT_WR:
    wasi_how = TCP_SHUTDOWN_TYPE_SEND;
    break;
  case SHUT_RDWR:
    wasi_how = TCP_SHUTDOWN_TYPE_BOTH;
    break;
  default:
    errno = EINVAL;
    return -1;
  }

  sockets_error_code_t error;
  sockets_borrow_tcp_socket_t socket_borrow =
      sockets_borrow_tcp_socket(socket->socket);
  if (!tcp_method_tcp_socket_shutdown(socket_borrow, wasi_how, &error)) {
    return __wasilibc_socket_error_to_errno(&error);
  }

  if (posix_how == SHUT_RD || posix_how == SHUT_RDWR) {
    // TODO wasi-sockets: drop input stream (if not already). And
    // update `recv` to take dropped input streams into account.
  }

  if (posix_how == SHUT_WR || posix_how == SHUT_RDWR) {
    // TODO wasi-sockets: drop output stream (if not already). And
    // update `send` to take dropped output streams into account.
  }
#else
  tcp_socket_state_connected_t *state = &socket->state.connected;
  bool close_receive = posix_how == SHUT_RD || posix_how == SHUT_RDWR;
  bool close_send = posix_how == SHUT_WR || posix_how == SHUT_RDWR;

  // If there's a thread blocked in I/O in read/write, then we can't cancel
  // that operation to close the stream. Return that this operation isn't
  // supported at this time.
  if ((close_receive && wasip3_io_state_blocked(&state->receive)) ||
      (close_send && wasip3_io_state_blocked(&state->send))) {
    errno = EOPNOTSUPP;
    return -1;
  }

  // Close out halves that are needed.
  //
  // Note the I/O states here may continue to get used by future syscalls, so
  // the internal lock pointer is reset to ensure that it's still pointing
  // to our still-valid lock.
  //
  // Note that closing the send half blocks to flush any buffered-but-unsent
  // data, so `shutdown(SHUT_WR)` can block here. See comments in
  // `wasip3_write_state_close` for some more discussion.
  if (close_receive) {
    wasip3_read_state_close(&state->receive);
#ifdef _REENTRANT
    state->receive.lock = &socket->lock;
#endif
  }

  if (close_send) {
    wasip3_write_state_close(&state->send);
#ifdef _REENTRANT
    state->send.lock = &socket->lock;
#endif
  }
#endif

  return 0;
}

#ifndef __wasip2__
static void tcp_connect_ready(void *data, poll_state_t *state,
                              wasip3_event_t *event) {
  tcp_socket_t *socket = (tcp_socket_t *)data;
  STRONG_LOCK(socket->lock);
  defer STRONG_UNLOCK(socket->lock);

  assert(socket->state.tag == TCP_SOCKET_STATE_CONNECTING);
  tcp_socket_state_connecting_t *conn = &socket->state.connecting;

  assert(conn->polling);
  conn->polling = false;

  if (!event)
    return;

  assert(event->event == WASIP3_EVENT_SUBTASK);
  assert(event->waitable == conn->subtask);
  assert(event->code == WASIP3_SUBTASK_RETURNED);
  wasip3_subtask_drop(conn->subtask);
  conn->subtask = 0;

  tcp_connect_finish(socket);
  short events = POLLWRNORM;
  if (socket->state.tag == TCP_SOCKET_STATE_CONNECT_FAILED)
    events |= POLLRDNORM;
  __wasilibc_poll_ready(state, events);
}

static void tcp_accept_ready(void *data, poll_state_t *state,
                             wasip3_event_t *event) {
  tcp_socket_t *socket = (tcp_socket_t *)data;
  STRONG_LOCK(socket->lock);
  defer STRONG_UNLOCK(socket->lock);

  assert(socket->state.tag == TCP_SOCKET_STATE_LISTENING);
  tcp_socket_state_listening_t *listen = &socket->state.listening;
  wasip3_accept_exit_blocking(socket);

  if (event) {
    wasip3_tcp_accept_finish_event(listen, event);
    __wasilibc_poll_ready(state, POLLRDNORM);
  }
}
#endif // !__wasip2__

static int tcp_poll_register(void *data, poll_state_t *state, short events) {
  tcp_socket_t *socket = (tcp_socket_t *)data;
  STRONG_LOCK(socket->lock);
  defer STRONG_UNLOCK(socket->lock);

  switch (socket->state.tag) {
  case TCP_SOCKET_STATE_CONNECTING: {
    if ((events & (POLLRDNORM | POLLWRNORM)) != 0) {
#ifdef __wasip2__
      return __wasilibc_poll_add(state, events, tcp_pollable(socket));
#else
      // If the `subtask` isn't listed here then that means that a thread is
      // blocked in `connect` while some parallel thread here is trying to
      // `poll` that socket. This isn't something supported by wasi-libc right
      // now.
      tcp_socket_state_connecting_t *conn = &socket->state.connecting;
      if (conn->subtask == 0) {
        errno = EOPNOTSUPP;
        return -1;
      }
      // Subtasks can only be in one waitable-set at a time, so if the connect
      // subtask is already registered with a `poll` — either another thread's
      // or this same `poll` call listing this socket twice — this can't be
      // supported. The flag is cleared in `tcp_connect_ready`, which `poll`
      // invokes both when the subtask completes and when the poll is torn
      // down.
      if (conn->polling) {
        errno = EOPNOTSUPP;
        return -1;
      }
      int rc =
          __wasilibc_poll_add(state, conn->subtask, tcp_connect_ready, data);
      if (rc == 0)
        conn->polling = true;
      return rc;
#endif
    }
    break;
  }

  case TCP_SOCKET_STATE_LISTENING: {
    // Listening sockets can only be ready to read, not write.
    if ((events & POLLRDNORM) != 0) {
#ifdef __wasip2__
      return __wasilibc_poll_add(state, events, tcp_pollable(socket));
#else
      tcp_socket_state_listening_t *listen = &socket->state.listening;
      // First make sure there's not another thread blocked on `accept`
      if (wasip3_accept_sync(socket, false) < 0)
        return -1;
      // Kick off an accept if it's not already going, then see what happened.
      //
      // If the accept is added to the poll set then
      // `wasip3_accept_enter_blocking` marks this socket as blocked for the
      // duration of the `poll`; `tcp_accept_ready` undoes that both when the
      // accept completes and when the poll is torn down.
      wasip3_tcp_accept_start(listen);
      if (listen->flags & (TCP_LISTENING_ACCEPT_READY | TCP_LISTENING_DONE)) {
        __wasilibc_poll_ready(state, POLLRDNORM);
        return 0;
      }
      assert(listen->flags & TCP_LISTENING_ACCEPTING);
      int rc =
          __wasilibc_poll_add(state, listen->stream, tcp_accept_ready, data);
      if (rc == 0)
        wasip3_accept_enter_blocking(socket);
      return rc;
#endif
    }
    break;
  }

  case TCP_SOCKET_STATE_CONNECTED: {
    tcp_socket_state_connected_t *conn = &socket->state.connected;
    if ((events & POLLRDNORM) != 0) {
#ifdef __wasip2__
      if (__wasilibc_poll_add_input_stream(
              state, streams_borrow_input_stream(conn->input),
              &conn->input_pollable) < 0)
        return -1;
#else
      if (__wasilibc_read_poll(&conn->receive, state) < 0)
        return -1;
#endif
    }
    if ((events & POLLWRNORM) != 0) {
#ifdef __wasip2__
      if (__wasilibc_poll_add_output_stream(
              state, streams_borrow_output_stream(conn->output),
              &conn->output_pollable) < 0)
        return -1;
#else
      if (__wasilibc_write_poll(&conn->send, state) < 0)
        return -1;
#endif
    }
    break;
  }

  case TCP_SOCKET_STATE_CONNECT_FAILED: {
    __wasilibc_poll_ready(state, events);
    break;
  }

  default:
    errno = ENOTSUP;
    return -1;
  }
  return 0;
}

#ifdef __wasip2__
static int tcp_poll_finish(void *data, poll_state_t *state, short events) {
  tcp_socket_t *socket = (tcp_socket_t *)data;

  switch (socket->state.tag) {
  case TCP_SOCKET_STATE_CONNECTING:
    break;
  case TCP_SOCKET_STATE_LISTENING:
    // Listening sockets can only be ready to read, not write.
    __wasilibc_poll_ready(state, events & POLLRDNORM);
    return 0;
  default:
    __wasilibc_poll_ready(state, events);
    return 0;
  }

  sockets_borrow_tcp_socket_t borrow =
      sockets_borrow_tcp_socket(socket->socket);
  tcp_tuple2_own_input_stream_own_output_stream_t tuple;
  tcp_error_code_t error;
  if (tcp_method_tcp_socket_finish_connect(borrow, &tuple, &error)) {
    socket->state.tag = TCP_SOCKET_STATE_CONNECTED;
    memset(&socket->state.connected, 0, sizeof(socket->state.connected));
    socket->state.connected.input = tuple.f0;
    socket->state.connected.output = tuple.f1;
    // Now that it's connected, it's immediately writable but not necessarily
    // immediately readable:
    __wasilibc_poll_ready(state, events & POLLWRNORM);
  } else if (error == NETWORK_ERROR_CODE_WOULD_BLOCK) {
    // No events yet -- application will need to poll again
  } else {
    socket->state.tag = TCP_SOCKET_STATE_CONNECT_FAILED;
    socket->state.connect_failed.error_code = error;
    __wasilibc_poll_ready(state, events);
  }
  return 0;
}
#endif

static int tcp_fcntl_getfl(void *data) {
  tcp_socket_t *socket = (tcp_socket_t *)data;
  STRONG_LOCK(socket->lock);
  defer STRONG_UNLOCK(socket->lock);

  int flags = 0;
  if (!socket->blocking) {
    flags |= O_NONBLOCK;
  }
  return flags;
}

static int tcp_fcntl_setfl(void *data, int flags) {
  tcp_socket_t *socket = (tcp_socket_t *)data;
  STRONG_LOCK(socket->lock);
  defer STRONG_UNLOCK(socket->lock);

  if (flags & O_NONBLOCK) {
    socket->blocking = false;
  } else {
    socket->blocking = true;
  }
  return 0;
}

static int tcp_getsockopt(void *data, int level, int optname,
                          void *restrict optval, socklen_t *restrict optlen) {
  tcp_socket_t *socket = (tcp_socket_t *)data;
  STRONG_LOCK(socket->lock);
  defer STRONG_UNLOCK(socket->lock);

  int value = 0;

  sockets_error_code_t error;
  sockets_borrow_tcp_socket_t socket_borrow =
      sockets_borrow_tcp_socket(socket->socket);

  switch (level) {
  case SOL_SOCKET:
    switch (optname) {
    case SO_TYPE:
      value = SOCK_STREAM;
      break;
    case SO_PROTOCOL:
      value = IPPROTO_TCP;
      break;

    case SO_DOMAIN:
      value = __wasilibc_wasi_family_to_libc(socket->family);
      break;

    case SO_ERROR:
      if (socket->state.tag == TCP_SOCKET_STATE_CONNECT_FAILED) {
        value = __wasilibc_map_socket_error(
            &socket->state.connect_failed.error_code);
      } else if (socket->state.tag == TCP_SOCKET_STATE_CONNECTING) {
        value = EINPROGRESS;
      } else {
        value = 0;
      }
      break;

    case SO_ACCEPTCONN: {
      bool is_listening = socket->state.tag == TCP_SOCKET_STATE_LISTENING;
      // Sanity check.
      if (is_listening !=
          sockets_method_tcp_socket_get_is_listening(socket_borrow)) {
        abort();
      }
      value = is_listening;
      break;
    }
    case SO_KEEPALIVE: {
      bool result;
      if (!sockets_method_tcp_socket_get_keep_alive_enabled(socket_borrow,
                                                            &result, &error))
        return __wasilibc_socket_error_to_errno(&error);
      value = result;
      break;
    }
    case SO_RCVBUF: {
      uint64_t result;
      if (!sockets_method_tcp_socket_get_receive_buffer_size(socket_borrow,
                                                             &result, &error))
        return __wasilibc_socket_error_to_errno(&error);

      value = result > INT_MAX ? INT_MAX : result;
      break;
    }
    case SO_SNDBUF: {
      uint64_t result;
      if (!sockets_method_tcp_socket_get_send_buffer_size(socket_borrow,
                                                          &result, &error))
        return __wasilibc_socket_error_to_errno(&error);

      value = result > INT_MAX ? INT_MAX : result;
      break;
    }
    case SO_REUSEADDR:
      value = socket->fake_reuseaddr;
      break;
    case SO_RCVTIMEO:
      return __wasilibc_getsockopt_timeout(socket->recv_timeout, optval,
                                           optlen);
    case SO_SNDTIMEO:
      return __wasilibc_getsockopt_timeout(socket->send_timeout, optval,
                                           optlen);
    default:
      errno = ENOPROTOOPT;
      return -1;
    }
    break;

  case SOL_IP:
    switch (optname) {
    case IP_TTL: {
      if (socket->family != SOCKETS_IP_ADDRESS_FAMILY_IPV4) {
        errno = EAFNOSUPPORT;
        return -1;
      }

      uint8_t result;
      if (!sockets_method_tcp_socket_get_hop_limit(socket_borrow, &result,
                                                   &error))
        return __wasilibc_socket_error_to_errno(&error);

      value = result;
      break;
    }
    default:
      errno = ENOPROTOOPT;
      return -1;
    }
    break;

  case SOL_IPV6:
    switch (optname) {
    case IPV6_UNICAST_HOPS: {
      if (socket->family != SOCKETS_IP_ADDRESS_FAMILY_IPV6) {
        errno = EAFNOSUPPORT;
        return -1;
      }

      uint8_t result;
      if (!sockets_method_tcp_socket_get_hop_limit(socket_borrow, &result,
                                                   &error))
        return __wasilibc_socket_error_to_errno(&error);

      value = result;
      break;
    }
    case IPV6_V6ONLY:
      if (socket->family != SOCKETS_IP_ADDRESS_FAMILY_IPV6) {
        errno = EAFNOSUPPORT;
        return -1;
      }
      // WASI IPv6 sockets are always v6-only.
      value = 1;
      break;
    default:
      errno = ENOPROTOOPT;
      return -1;
    }
    break;

  case SOL_TCP:
    switch (optname) {
    case TCP_NODELAY: {
      value = socket->fake_nodelay;
      break;
    }
    case TCP_KEEPIDLE: {
      sockets_duration_t result_ns;
      if (!sockets_method_tcp_socket_get_keep_alive_idle_time(
              socket_borrow, &result_ns, &error))
        return __wasilibc_socket_error_to_errno(&error);

      uint64_t result_s = result_ns / NS_PER_S;
      if (result_s == 0) {
        result_s = 1; // Value was rounded down to zero. Round it up instead,
                      // because 0 is an invalid value for this socket option.
      }

      value = result_s > INT_MAX ? INT_MAX : result_s;
      break;
    }
    case TCP_KEEPINTVL: {
      sockets_duration_t result_ns;
      if (!sockets_method_tcp_socket_get_keep_alive_interval(
              socket_borrow, &result_ns, &error))
        return __wasilibc_socket_error_to_errno(&error);

      uint64_t result_s = result_ns / NS_PER_S;
      if (result_s == 0) {
        result_s = 1; // Value was rounded down to zero. Round it up instead,
                      // because 0 is an invalid value for this socket option.
      }

      value = result_s > INT_MAX ? INT_MAX : result_s;
      break;
    }
    case TCP_KEEPCNT: {
      uint32_t result;
      if (!sockets_method_tcp_socket_get_keep_alive_count(socket_borrow,
                                                          &result, &error))
        return __wasilibc_socket_error_to_errno(&error);

      value = result > INT_MAX ? INT_MAX : result;
      break;
    }
    default:
      errno = ENOPROTOOPT;
      return -1;
    }
    break;

  default:
    errno = ENOPROTOOPT;
    return -1;
  }

  // Copy out integer value.
  memcpy(optval, &value, *optlen < sizeof(int) ? *optlen : sizeof(int));
  *optlen = sizeof(int);
  return 0;
}

static int tcp_setsockopt(void *data, int level, int optname,
                          const void *optval, socklen_t optlen) {
  tcp_socket_t *socket = (tcp_socket_t *)data;
  STRONG_LOCK(socket->lock);
  defer STRONG_UNLOCK(socket->lock);

  if (optlen < sizeof(int)) {
    errno = EINVAL;
    return -1;
  }
  int intval = *(int *)optval;

  sockets_error_code_t error;
  sockets_borrow_tcp_socket_t socket_borrow =
      sockets_borrow_tcp_socket(socket->socket);

  switch (level) {
  case SOL_SOCKET:
    switch (optname) {
    case SO_KEEPALIVE: {
      if (!sockets_method_tcp_socket_set_keep_alive_enabled(
              socket_borrow, intval != 0, &error))
        return __wasilibc_socket_error_to_errno(&error);

      return 0;
    }
    case SO_RCVBUF: {
      if (!sockets_method_tcp_socket_set_receive_buffer_size(socket_borrow,
                                                             intval, &error))
        return __wasilibc_socket_error_to_errno(&error);

      return 0;
    }
    case SO_SNDBUF: {
      if (!sockets_method_tcp_socket_set_send_buffer_size(socket_borrow, intval,
                                                          &error))
        return __wasilibc_socket_error_to_errno(&error);

      return 0;
    }
    case SO_REUSEADDR: {
      // As of this writing, WASI has no support for changing SO_REUSEADDR
      // -- it's enabled by default and cannot be disabled.  To keep
      // applications happy, we pretend to support enabling and disabling
      // it.
      socket->fake_reuseaddr = (intval != 0);
      return 0;
    }
    case SO_RCVTIMEO:
      return __wasilibc_setsockopt_timeout(optval, optlen,
                                           &socket->recv_timeout);
    case SO_SNDTIMEO:
      return __wasilibc_setsockopt_timeout(optval, optlen,
                                           &socket->send_timeout);
    default:
      errno = ENOPROTOOPT;
      return -1;
    }
    break;

  case SOL_IP:
    switch (optname) {
    case IP_TTL: {
      if (socket->family != SOCKETS_IP_ADDRESS_FAMILY_IPV4) {
        errno = EAFNOSUPPORT;
        return -1;
      }

      if (intval < 0 || intval > UINT8_MAX) {
        errno = EINVAL;
        return -1;
      }

      if (!sockets_method_tcp_socket_set_hop_limit(socket_borrow, intval,
                                                   &error))
        return __wasilibc_socket_error_to_errno(&error);

      return 0;
    }
    default:
      errno = ENOPROTOOPT;
      return -1;
    }
    break;

  case SOL_IPV6:
    switch (optname) {
    case IPV6_UNICAST_HOPS: {
      if (socket->family != SOCKETS_IP_ADDRESS_FAMILY_IPV6) {
        errno = EAFNOSUPPORT;
        return -1;
      }

      if (intval < 0 || intval > UINT8_MAX) {
        errno = EINVAL;
        return -1;
      }

      if (!sockets_method_tcp_socket_set_hop_limit(socket_borrow, intval,
                                                   &error))
        return __wasilibc_socket_error_to_errno(&error);

      return 0;
    }
    case IPV6_V6ONLY:
      if (socket->family != SOCKETS_IP_ADDRESS_FAMILY_IPV6) {
        errno = EAFNOSUPPORT;
        return -1;
      }
      // WASI IPv6 sockets are always v6-only; accept setting it to the
      // default (enabled) but reject any attempt to disable it.
      if (intval == 0) {
        errno = EOPNOTSUPP;
        return -1;
      }
      return 0;
    default:
      errno = ENOPROTOOPT;
      return -1;
    }
    break;

  case SOL_TCP:
    switch (optname) {
    case TCP_NODELAY: {
      // At the time of writing, WASI has no support for TCP_NODELAY.
      // Yet, many applications expect this option to be implemented.
      // To ensure those applications can run on WASI at all, we fake
      // support for it by recording the value, but not doing anything
      // with it.
      // If/when WASI adds true support, we can remove this workaround
      // and implement it properly. From the application's perspective
      // the "worst" thing that can then happen is that it automagically
      // becomes faster.
      socket->fake_nodelay = (intval != 0);
      return 0;
    }
    case TCP_KEEPIDLE: {
      sockets_duration_t duration = intval * NS_PER_S;
      if (!sockets_method_tcp_socket_set_keep_alive_idle_time(socket_borrow,
                                                              duration, &error))
        return __wasilibc_socket_error_to_errno(&error);

      return 0;
    }
    case TCP_KEEPINTVL: {
      sockets_duration_t duration = intval * NS_PER_S;
      if (!sockets_method_tcp_socket_set_keep_alive_interval(socket_borrow,
                                                             duration, &error))
        return __wasilibc_socket_error_to_errno(&error);

      return 0;
    }
    case TCP_KEEPCNT: {
      if (!sockets_method_tcp_socket_set_keep_alive_count(socket_borrow, intval,
                                                          &error))
        return __wasilibc_socket_error_to_errno(&error);

      return 0;
    }
    default:
      errno = ENOPROTOOPT;
      return -1;
    }
    break;

  default:
    errno = ENOPROTOOPT;
    return -1;
  }

  // should not be reachable, all cases above should return
  abort();
}

static descriptor_vtable_t tcp_vtable = {
    .free = tcp_free,

    .get_read_stream = tcp_get_read_stream,
    .get_write_stream = tcp_get_write_stream,
    .set_blocking = tcp_set_blocking,
    .fstat = tcp_fstat,

    .accept4 = tcp_accept4,
    .bind = tcp_bind,
    .connect = tcp_connect,
    .getsockname = tcp_getsockname,
    .getpeername = tcp_getpeername,
    .listen = tcp_listen,
    .recvfrom = tcp_recvfrom,
    .sendto = tcp_sendto,
    .shutdown = tcp_shutdown,
    .getsockopt = tcp_getsockopt,
    .setsockopt = tcp_setsockopt,
    .poll_register = tcp_poll_register,
#ifdef __wasip2__
    .poll_finish = tcp_poll_finish,
#endif

    .fcntl_getfl = tcp_fcntl_getfl,
    .fcntl_setfl = tcp_fcntl_setfl,
};

#endif // not(__wasip1__)
