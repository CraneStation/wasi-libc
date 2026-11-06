#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "test.h"

int main(void) {
  int fd = open(".", O_RDONLY | O_DIRECTORY);
  if (fd < 0 || close(fd) != 0)
    return 1;
  struct allocation {
    struct allocation *next;
  } *head = NULL, *node;
  while ((node = malloc(sizeof(*node))) != NULL) {
    node->next = head;
    head = node;
  }
  struct dirent **names;
  int result, error;
  // More attempts than the host descriptor limit expose a leaked descriptor.
  for (int i = 0; i < 256; i++) {
    result = scandir(".", &names, NULL, NULL);
    error = errno;
    if (result != -1 || error != ENOMEM)
      break;
  }
  while (head != NULL) {
    node = head;
    head = head->next;
    free(node);
  }
  if (result != -1 || error != ENOMEM)
    t_error("expected scandir to report ENOMEM\n");
  if (result >= 0) {
    for (int i = 0; i < result; i++)
      free(names[i]);
    free(names);
  }
  fd = open(".", O_RDONLY | O_DIRECTORY);
  if (fd < 0)
    t_error("cannot open directory after freeing memory\n");
  else if (close(fd) != 0)
    t_error("cannot close directory\n");
  return t_status;
}
