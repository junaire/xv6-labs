//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "fcntl.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}

// fill the data from the open file.
void mmapread(struct vma* v, uint64 va) {
  ilock(v->f->ip);

  int n;
  if (v->length <= v->f->ip->size)
    n = v->length;
  else
    n = v->f->ip->size;
  int r;
  if((r = readi(v->f->ip, 1, va, 0, n)) > 0) {
    // v->f->off += r;
  }

  iunlock(v->f->ip);
}

uint64
sys_mmap(void) {
  int length, prot, flags, fd, i;
  struct proc* p;
  struct vma* v;
  struct file* f;

  if (argint(1, &length) < 0|| argint(2, &prot) < 0|| argint(3, &flags) < 0|| argint(4, &fd) < 0)
    return -1;

  p = myproc();
  // get the struct file* via the fd.
  if(fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0)
    return -1;
  // check that mmap doesn't allow read/write mapping of a
  // file opened read-only.
  if ((prot & PROT_READ) && (prot & PROT_WRITE) && (flags & MAP_SHARED) && !f->writable)
    return -1;


  for (i = 0; i < 16; ++i) {
    v = &p->vmas[i];
    // found an empty vma.
    if (v->f == 0) {
      v->length = length;
      v->prot = prot;
      v->flags = flags;
      v->is_alloc = 0;

      v->f = filedup(f);

      if (i == 0)
        v->addr = PGROUNDUP(p->sz);
      else
        v->addr = p->vmas[i-1].addr + p->vmas[i-1].length;

      // printf("mmap: found an empty entry vma[%d] {.f =%p, .addr =%p, .length=%d}\n", i, v->f, v->addr, v->length);
      return (uint64)v->addr;
    }
  }
  return -1;
}

uint64
sys_munmap(void) {
  struct proc* p;
  struct vma* v;
  int i, length, npages;
  uint64 addr;

  if (argaddr(0, &addr) < 0|| argint(1, &length) < 0)
    return -1;

  npages = (length + PGSIZE - 1) / PGSIZE;
  p = myproc();

  for (i = 0; i < 16; ++i) {
    v = &p->vmas[i];
    if (v->f != 0 && addr >= v->addr && addr < (v->addr + v->length)) {
      // write unmaped pages back.
      if ((v->flags & MAP_SHARED) && addr == v->addr)
        filewrite(v->f, addr, length);

      // printf("unmap vma[%d] addr=%p length=%d npages=%d\n", i, v->addr, length, npages);
      uvmunmap(p->pagetable, addr, npages, 1);

      if (addr == v->addr && length < v->length) {
        v->addr += length;
        v->length -= length;
      } else {
        if (v->f != 0)
          fileclose(v->f);
        v->f = 0;
        v->is_alloc = 0;
      }
      return 0;
    }
  }
  return -1;
}
