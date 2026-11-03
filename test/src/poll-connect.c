#include "test.h"
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define TEST(c)                                                                \
  do {                                                                         \
    errno = 0;                                                                 \
    if (!(c))                                                                  \
      t_error("%s failed (errno = %d)\n", #c, errno);                          \
  } while (0)

#define TEST2(c)                                                               \
  do {                                                                         \
    if (!(c))                                                                  \
      t_error("%s failed (errno = %d)\n", #c, errno);                          \
  } while (0)

#define ASSERT(c)                                                              \
  do {                                                                         \
    if (!(c))                                                                  \
      t_error("%s failed\n", #c);                                              \
  } while (0)

int main() {
  errno = 0;
  int server = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  TEST2(server != -1);

  // Bind to any available port on the loopback address.
  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  server_address.sin_port = htons(0);
  TEST(bind(server, (struct sockaddr *)&server_address,
            sizeof(server_address)) != -1);

  // Get the port we bound to.
  socklen_t server_address_len = sizeof(server_address);
  TEST(getsockname(server, (struct sockaddr *)&server_address,
                   &server_address_len) != -1);

  TEST(listen(server, 1) != -1);

  errno = 0;
  int client = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  TEST2(client != -1);

  // Connect a client socket.  This may or may not complete immediately; if not,
  // we'll poll and finish connecting later.
  TEST(connect(client, (const struct sockaddr *)&server_address,
               sizeof(server_address)) != -1 ||
       errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS);

  // Start accepting connections.  This may or may not complete immediately; if
  // not, we'll poll and accept again later.
  socklen_t client_len = sizeof(struct sockaddr_in);
  struct sockaddr_in client_address;
  errno = 0;
  int server_client =
      accept(server, (struct sockaddr *)&client_address, &client_len);
  TEST2(server_client != -1 || errno == EAGAIN || errno == EWOULDBLOCK ||
        errno == EINPROGRESS);

  struct pollfd client_fd = {
      .fd = client, .events = POLLWRNORM | POLLRDNORM, .revents = 0};

  struct pollfd fds[2];
  fds[0] = client_fd;

  // Poll until the client socket finished connecting and the server socket
  // finishes accepting.
  int loop_count = 0;
  while ((server_client == -1 || fds[0].events) && loop_count < 10) {
    if (t_status)
      exit(t_status);

    ++loop_count;

    int fd_count = 1;
    if (server_client == -1) {
      struct pollfd server_fd = {
          .fd = server, .events = POLLWRNORM | POLLRDNORM, .revents = 0};
      fds[1] = server_fd;
      fd_count = 2;
    }

    TEST(poll(fds, fd_count, 100) != -1);
    if (fds[0].revents) {
      // The newly-connected client socket should be writable but not yet
      // readable:
      ASSERT((fds[0].revents & POLLRDNORM) == 0);
      ASSERT((fds[0].revents & POLLWRNORM) != 0);
      fds[0].events = 0;
    }

    if (server_client == -1 && fds[1].revents) {
      // A listening socket should be readable but not writable:
      ASSERT((fds[1].revents & POLLRDNORM) != 0);
      ASSERT((fds[1].revents & POLLWRNORM) == 0);
      errno = 0;
      server_client =
          accept(server, (struct sockaddr *)&client_address, &client_len);
      TEST2(server_client != -1);
    }
  }
  ASSERT(loop_count < 10);

  int one = 1;
  TEST(ioctl(server_client, FIONBIO, &one) != -1);

  // Test that we can `recv` what we `send`
  {
    uint8_t data[5] = {1, 2, 3, 4, 5};
    while (!t_status) {
      // Send some data from the server to the client.
      ssize_t sent = send(server_client, data, sizeof(data), 0);
      if (sent != -1) {
        TEST2(sent == sizeof(data));
        break;
      }
      if (errno != EWOULDBLOCK)
        t_error("send failed (errno = %d)\n", errno);
      struct pollfd fd = {
          .fd = server_client, .events = POLLWRNORM, .revents = 0};
      TEST(poll(&fd, 1, -1) != -1);
    }

    fds[0].events = POLLRDNORM;
    errno = 0;
    int count = poll(fds, 1, 100);
    TEST2(count == 1);
    // Now the server has sent something, so the client socket should be
    // readable.
    ASSERT((fds[0].revents & POLLRDNORM) != 0);

    uint8_t received[sizeof(data)];
    TEST(recv(client, received, sizeof(data), 0) == sizeof(data));

    // Assert that what was received matches what was sent.
    for (size_t i = 0; i < sizeof(data); ++i) {
      ASSERT(received[i] == data[i]);
    }
  }

  // Test that we can `read` what we `write` and that neither of them block.
  {
    size_t data_len = 16 * 1024;
    errno = 0;
    uint8_t *received = malloc(data_len);
    TEST2(received != NULL);

    // Nothing should be readable, yet.
    TEST(read(client, received, data_len) == -1 &&
         (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS));

    errno = 0;
    uint8_t *data = malloc(data_len);
    TEST2(data != NULL);

    for (size_t i = 0; i < data_len; ++i) {
      data[i] = 42;
    }

    // The socket may not be immediately writable here since the host may
    // still be flushing the previous `send` above, so wait for it to become
    // writable.
    {
      struct pollfd fd = {
          .fd = server_client, .events = POLLWRNORM, .revents = 0};
      errno = 0;
      TEST2(poll(&fd, 1, 5000) == 1);
    }

    // Write until we hit backpressure, keeping track of how much was written.
    size_t write_total = 0;
    while (1) {
      if (t_status)
        exit(t_status);

      errno = 0;
      ssize_t count = write(server_client, data, data_len);
      if (count == -1) {
        TEST2(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS);
        break;
      }
      write_total += count;
    }
    TEST2(write_total > 0);

    fds[0].events = POLLRDNORM;

    // Read everything that was written. All of it is guaranteed to arrive, so
    // use a generous poll timeout to avoid spurious failures on slow hosts.
    size_t read_total = 0;
    while (read_total < write_total) {
      if (t_status)
        exit(t_status);

      {
        errno = 0;
        int count = poll(fds, 1, 5000);
        TEST2(count == 1);
        ASSERT((fds[0].revents & POLLRDNORM) != 0);
      }

      errno = 0;
      ssize_t count = read(client, received, data_len);
      TEST2(count != -1 || errno == EAGAIN || errno == EWOULDBLOCK ||
            errno == EINPROGRESS);

      if (count > 0) {
        read_total += count;
        for (ssize_t i = 0; i < count; ++i) {
          ASSERT(received[i] == 42);
        }
      }
    }
    ASSERT(read_total == write_total);

    // Nothing more should be readable.
    TEST(read(client, received, data_len) == -1 &&
         (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS));
  }

  return t_status;
}
