#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), '\0', DIRSIZ-strlen(p));
  return buf;
}

void
do_find(char *path, char* filename)
{
  char buf[512], *p;
  memset(buf, 0, sizeof(buf));
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch (st.type) {
  case T_FILE: {
    fprintf(2, "find: cannot find in %s, which is a file\n", path);
    break;
  }

  case T_DIR: {
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)) {

      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;

      if(stat(buf, &st) < 0) {
        printf("find: cannot stat %s\n", buf);
        continue;
      }

      // buf <=> ./a/b/c
      // path <=> c
      switch (st.type) {
        case T_FILE: {
          path = fmtname(buf);
          if (strcmp(path, filename) == 0) {
            printf("%s\n", buf);
          }
          break;
        }
        case T_DIR: {
          path = fmtname(buf);
          if (strcmp(path, ".") != 0 && strcmp(path, "..") != 0)
            // printf("DIR: buf=%s, path=%s\n", buf, path);
            do_find(buf, filename);
          break;
        }
      }
    }
    break;
  }
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "Usage: find <path> <filename>");
    exit(1);
  }
  char* path = argv[1];
  char* filename = argv[2];
  do_find(path, filename);
  exit(0);
}
