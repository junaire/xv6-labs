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

int
main(int argc, char *argv[])
{
  int p1[2]; // p1[0] <-> child's stdin  | p1[1] <-> parent's stdout
  int p2[2]; // p2[0] <-> parent's stdin | p2[1] <-> child's stdout

  if(pipe(p1) < 0)
    panic("pipe");

  if(pipe(p2) < 0)
    panic("pipe");

  if (fork1() == 0) {
    char buf1[2] = {'1', 0};
    write(p2[1], buf1, 2);

    char buf2[2];
    while (read(p1[0], buf2, 2) == -1)
      ;
    fprintf(1,"%d: received pong\n", getpid());
    exit(0);
  } else {
    char buf2[2];
    while (read(p2[0], buf2, 2) == -1)
      ;
    fprintf(1,"%d: received ping\n", getpid());
    char buf1[2] = {'1', 0};
    write(p1[1], buf1, 2);
    wait(0);
    exit(0);
  }
  return 0;
}
