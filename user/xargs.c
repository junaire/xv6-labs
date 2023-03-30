#include "kernel/param.h"
#include "kernel/types.h"
#include "user/user.h"

int readline(char *new_argv[MAXARG], int curr_argc) {
  char buf[1024];
  int n = 0;
  while (read(0, buf + n, 1)) {
    if (buf[n] == '\n') {
      break;
    }
    n++;
  }
  buf[n] = 0;
  if (n == 0)
    return 0;
  int offset = 0;
  while (offset < n) {
    new_argv[curr_argc++] = buf + offset;
    while (buf[offset] != ' ' && offset < n) {
      offset++;
    }
    while (buf[offset] == ' ' && offset < n) {
      buf[offset++] = 0;
    }
  }
  return curr_argc;
}

int main(int argc, char *argv[]) {
  char *new_argv[MAXARG];
  for (int i = 1; i < argc; ++i)
    new_argv[i - 1] = argv[i];

  int curr_argc;
  while ((curr_argc = readline(new_argv, argc - 1)) != 0) {
    new_argv[curr_argc] = 0;
    if (fork() == 0) {
      exec(new_argv[0], new_argv);
    }
    wait(0);
  }
  exit(0);
}
