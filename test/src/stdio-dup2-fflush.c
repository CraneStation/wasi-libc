#include <stdio.h>
#include <unistd.h>

int main(void) {
  // Write "firstline" to stderr using both stderr and a dup of stderr
  write(STDERR_FILENO, "first", 5);
  if (dup2(STDERR_FILENO, STDOUT_FILENO) != 1) {
    return 1;
  }
  printf("line");
  fflush(stdout); // Should flush stderr

  // Write "secondline" using just the original stderr handle
  write(STDERR_FILENO, "secondline\n", 11);

  return 0;
}
