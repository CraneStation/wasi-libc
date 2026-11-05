#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "test.h"

int main(void) {
  int before = open(".", O_RDONLY | O_DIRECTORY);
  if (before < 0 || close(before) != 0)
    return 1;
  struct allocation {
    struct allocation *next;
  } *head = NULL, *node;
  while ((node = malloc(sizeof(*node))) != NULL) {
    node->next = head;
    head = node;
  }
  struct dirent **names;
  int result = scandir(".", &names, NULL, NULL);
  int error = errno;
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
  int after = open(".", O_RDONLY | O_DIRECTORY);
  if (after != before)
    t_error("failed scandir leaked a descriptor\n");
  if (after >= 0)
    close(after);
  return t_status;
}
