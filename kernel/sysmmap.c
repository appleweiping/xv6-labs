//
// mmap / munmap system calls (MIT 6.S081 mmap lab).
//
// Files are mapped lazily: mmap() only records a VMA, and the actual pages
// are allocated and filled from the file on demand in the page-fault handler
// (see mmap_fault() below, called from usertrap()). munmap() writes dirty
// MAP_SHARED pages back to the file and unmaps the range.
//
#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "fcntl.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"

// mmap regions live just below the trapframe and grow downward, so they never
// collide with the heap (which grows upward via sbrk).
#define MMAPTOP (TRAPFRAME)

// Find the lowest currently-mapped mmap address for process p, or MMAPTOP if
// there are no mappings. New regions are placed below this.
static uint64
mmap_lowest(struct proc *p)
{
  uint64 low = MMAPTOP;
  for(int i = 0; i < NVMA; i++){
    if(p->vmas[i].used && p->vmas[i].addr < low)
      low = p->vmas[i].addr;
  }
  return low;
}

uint64
sys_mmap(void)
{
  uint64 addr;
  int length, prot, flags, fd, offset;
  struct proc *p = myproc();
  struct file *f;

  if(argaddr(0, &addr) < 0 || argint(1, &length) < 0 ||
     argint(2, &prot) < 0 || argint(3, &flags) < 0 ||
     argint(4, &fd) < 0 || argint(5, &offset) < 0)
    return -1;

  if(length <= 0 || offset < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0)
    return -1;

  // A MAP_SHARED, writable mapping of a file requires the file to be writable,
  // otherwise writes could not be flushed back. (A MAP_PRIVATE writable
  // mapping is fine on a read-only file since changes stay private.)
  if((prot & PROT_READ) && !f->readable)
    return -1;
  if((prot & PROT_WRITE) && (flags & MAP_SHARED) && !f->writable)
    return -1;

  // Find a free VMA slot.
  struct vma *v = 0;
  for(int i = 0; i < NVMA; i++){
    if(!p->vmas[i].used){
      v = &p->vmas[i];
      break;
    }
  }
  if(v == 0)
    return -1;

  // Place the region on a page boundary below the lowest existing mapping.
  uint64 sz = PGROUNDUP(length);
  uint64 va = mmap_lowest(p) - sz;
  if(va < p->sz)            // would run into the heap
    return -1;

  v->used = 1;
  v->addr = va;
  v->length = length;
  v->prot = prot;
  v->flags = flags;
  v->offset = offset;
  v->f = filedup(f);        // keep the file alive until munmap

  return va;
}

// Page-fault handler for lazily-allocated mmap pages. va is the faulting
// address. Returns 0 on success, -1 if the fault does not belong to any VMA
// (i.e. a genuine segfault).
int
mmap_fault(uint64 va)
{
  struct proc *p = myproc();
  struct vma *v = 0;

  va = PGROUNDDOWN(va);

  for(int i = 0; i < NVMA; i++){
    struct vma *c = &p->vmas[i];
    if(c->used && va >= c->addr && va < c->addr + c->length){
      v = c;
      break;
    }
  }
  if(v == 0)
    return -1;

  char *mem = kalloc();
  if(mem == 0)
    return -1;
  memset(mem, 0, PGSIZE);

  // Read the corresponding slice of the file into the page. Bytes past the
  // end of the file stay zero (readi may return fewer than PGSIZE bytes).
  uint off = v->offset + (va - v->addr);
  ilock(v->f->ip);
  readi(v->f->ip, 0, (uint64)mem, off, PGSIZE);
  iunlock(v->f->ip);

  int perm = PTE_U;
  if(v->prot & PROT_READ)
    perm |= PTE_R;
  if(v->prot & PROT_WRITE)
    perm |= PTE_W;
  if(v->prot & PROT_EXEC)
    perm |= PTE_X;

  if(mappages(p->pagetable, va, PGSIZE, (uint64)mem, perm) != 0){
    kfree(mem);
    return -1;
  }
  return 0;
}

static void mmap_writeback(struct proc *p, struct vma *v, uint64 addr, uint64 len);

// Tear down every mmap region of process p (used on exit). Flushes dirty
// MAP_SHARED pages, unmaps present pages, and releases the file references.
void
mmap_cleanup(struct proc *p)
{
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vmas[i];
    if(!v->used)
      continue;
    uint64 start = PGROUNDDOWN(v->addr);
    uint64 end = PGROUNDUP(v->addr + v->length);
    mmap_writeback(p, v, start, end - start);
    uvmunmap_lazy(p->pagetable, start, (end - start) / PGSIZE);
    fileclose(v->f);
    v->used = 0;
  }
}

// Write the mapped range [addr, addr+len) back to the file if it is a dirty,
// writable, MAP_SHARED mapping. Only pages actually present are written.
static void
mmap_writeback(struct proc *p, struct vma *v, uint64 addr, uint64 len)
{
  if(!(v->flags & MAP_SHARED) || !(v->prot & PROT_WRITE))
    return;

  for(uint64 a = addr; a < addr + len; a += PGSIZE){
    pte_t *pte = walk_lookup(p->pagetable, a);
    if(pte == 0 || (*pte & PTE_V) == 0)
      continue;                       // page never faulted in
    if((*pte & PTE_D) == 0)
      continue;                       // page not dirty, nothing to flush

    uint off = v->offset + (a - v->addr);
    uint n = PGSIZE;
    if(a + n > v->addr + v->length)   // don't write past the mapping
      n = (v->addr + v->length) - a;

    begin_op();
    ilock(v->f->ip);
    writei(v->f->ip, 1, a, off, n);
    iunlock(v->f->ip);
    end_op();
  }
}

uint64
sys_munmap(void)
{
  uint64 addr;
  int length;
  struct proc *p = myproc();

  if(argaddr(0, &addr) < 0 || argint(1, &length) < 0)
    return -1;
  if(length <= 0)
    return -1;

  // Find the VMA that contains this range.
  struct vma *v = 0;
  for(int i = 0; i < NVMA; i++){
    struct vma *c = &p->vmas[i];
    if(c->used && addr >= c->addr && addr < c->addr + c->length){
      v = c;
      break;
    }
  }
  if(v == 0)
    return -1;

  // The tests only ever unmap a prefix or the whole region (never a hole in
  // the middle), so we support unmapping from the start or the end.
  uint64 start = PGROUNDDOWN(addr);
  uint64 end = PGROUNDUP(addr + length);

  // Flush dirty shared pages, then drop the mappings for present pages.
  mmap_writeback(p, v, start, end - start);
  uvmunmap_lazy(p->pagetable, start, (end - start) / PGSIZE);

  // Shrink the VMA. If we unmapped the beginning, advance addr and offset.
  if(addr <= v->addr){
    uint64 removed = end - v->addr;
    if(removed >= v->length){
      // whole region gone
      fileclose(v->f);
      v->used = 0;
    } else {
      v->addr = end;
      v->offset += removed;
      v->length -= removed;
    }
  } else {
    // unmapping a suffix
    v->length = addr - v->addr;
    if(v->length == 0){
      fileclose(v->f);
      v->used = 0;
    }
  }

  return 0;
}
