// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// Reference count for every physical page, for copy-on-write fork. Indexed by
// physical page number relative to KERNBASE. A page is freed only when its
// count reaches zero.
#define PA2PGIDX(pa) (((uint64)(pa) - KERNBASE) / PGSIZE)
#define NPHYPAGES    (PA2PGIDX(PHYSTOP) + 1)

struct {
  struct spinlock lock;
  int count[NPHYPAGES];
} kref;

// Increment the reference count of the physical page containing pa.
void
kref_incr(void *pa)
{
  acquire(&kref.lock);
  kref.count[PA2PGIDX(pa)]++;
  release(&kref.lock);
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&kref.lock, "kref");
  // freerange -> kfree assumes count==1, so start every page at 1.
  for(int i = 0; i < NPHYPAGES; i++)
    kref.count[i] = 1;
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Drop one reference; only actually free the page at the last reference.
  acquire(&kref.lock);
  if(--kref.count[PA2PGIDX(pa)] > 0){
    release(&kref.lock);
    return;
  }
  release(&kref.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    // A freshly allocated page has exactly one reference.
    acquire(&kref.lock);
    kref.count[PA2PGIDX(r)] = 1;
    release(&kref.lock);
  }
  return (void*)r;
}
