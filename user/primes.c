#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}


void spawn_process(int p[2])
{
  close(p[1]); // Close unused write end.
  int first;
  // No need to spinning, it will synchronize automatically.
  read(p[0], &first, sizeof(first));
  fprintf(1, "prime %d\n", first);

  int rest;
  int is_end = read(p[0], &rest, sizeof(rest));
  if (!is_end)
    exit(0);

  int newp[2];
  if (pipe(newp) < 0)
    panic("new pipe");
  if (fork1() == 0) {
    spawn_process(newp);
  } else {
    close(newp[0]);

    // The first one.
    if (rest % first != 0)
      write(newp[1], &rest, sizeof(rest));

    while (read(p[0], &rest, sizeof(rest))) {
      if (rest % first != 0)
        write(newp[1], &rest, sizeof(rest));
    }
    close(newp[1]);
    close(p[0]);
    wait(0);
  }
  exit(0);
}

int
main(int argc, char *argv[])
{
  int p[2];
  int i;
  if (pipe(p) < 0)
    panic("pipe");

  if (fork1() == 0) {
    spawn_process(p);
    exit(0);
  } else {
    close(p[0]); // Close unused read end.
    for (i = 2; i <= 35; ++i)
      write(p[1], &i, sizeof(i));
    close(p[1]); // Now close the pipe, the child process will end afterwards.
    wait(0); // DO NOT PUT THIS BEFORE CLOSE THE PIPE, OR DEAD LOCK.
    exit(0);
  }
  return 0;
}
