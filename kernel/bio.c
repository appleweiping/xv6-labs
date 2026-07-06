// Buffer cache.
//
// The buffer cache is a hash table of buf structures holding cached copies of
// disk block contents.  Caching disk blocks in memory reduces the number of
// disk reads and also provides a synchronization point for disk blocks used
// by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
//
// This version replaces the single global bcache lock and LRU list with a
// hash table of NBUCKET buckets, each with its own lock, to reduce lock
// contention (lab lock, part 2). Buffers are recycled using a per-buffer
// timestamp (the value of `ticks` when the buffer last became free) rather
// than a linked LRU list.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13
#define HASH(blockno) ((blockno) % NBUCKET)

struct bucket {
  struct spinlock lock;
  struct buf head;   // sentinel node of this bucket's list
};

struct {
  struct spinlock lock;   // protects allocation/eviction across buckets
  struct buf buf[NBUF];
  struct bucket buckets[NBUCKET];
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for(int i = 0; i < NBUCKET; i++){
    initlock(&bcache.buckets[i].lock, "bcache");
    bcache.buckets[i].head.next = 0;
  }

  // Put all buffers into bucket 0 initially.
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->refcnt = 0;
    b->timestamp = 0;
    b->next = bcache.buckets[0].head.next;
    bcache.buckets[0].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int id = HASH(blockno);

  acquire(&bcache.buckets[id].lock);

  // Is the block already cached in its bucket?
  for(b = bcache.buckets[id].head.next; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buckets[id].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached. We must find a free buffer to recycle, possibly from another
  // bucket. Release the bucket lock and take the global allocation lock to
  // serialize eviction (this avoids two CPUs racing to steal the same buffer
  // and prevents deadlock from acquiring two bucket locks in an inconsistent
  // order).
  release(&bcache.buckets[id].lock);
  acquire(&bcache.lock);

  // Re-check: another CPU may have cached this block while we waited.
  acquire(&bcache.buckets[id].lock);
  for(b = bcache.buckets[id].head.next; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buckets[id].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.buckets[id].lock);

  // Find the least-recently-used free buffer across all buckets. Only ever
  // hold one bucket lock at a time here: the one containing the current best
  // victim. The global bcache.lock (held throughout) serializes eviction, so
  // the chosen victim's refcnt/timestamp cannot change under us.
  struct buf *victim = 0;
  int victim_bucket = -1;
  for(int i = 0; i < NBUCKET; i++){
    acquire(&bcache.buckets[i].lock);
    struct buf *best = 0;
    for(b = bcache.buckets[i].head.next; b != 0; b = b->next){
      if(b->refcnt == 0 &&
         (best == 0 || b->timestamp < best->timestamp))
        best = b;
    }
    if(best && (victim == 0 || best->timestamp < victim->timestamp)){
      // New best victim in bucket i: drop the previous victim's bucket lock
      // and keep bucket i locked.
      if(victim_bucket != -1)
        release(&bcache.buckets[victim_bucket].lock);
      victim = best;
      victim_bucket = i;
    } else {
      release(&bcache.buckets[i].lock);
    }
  }

  if(victim == 0)
    panic("bget: no buffers");

  // Remove victim from its current bucket.
  if(victim_bucket != id){
    struct buf **pp = &bcache.buckets[victim_bucket].head.next;
    while(*pp != victim)
      pp = &(*pp)->next;
    *pp = victim->next;
    release(&bcache.buckets[victim_bucket].lock);

    // Insert victim into the target bucket.
    acquire(&bcache.buckets[id].lock);
    victim->next = bcache.buckets[id].head.next;
    bcache.buckets[id].head.next = victim;
  }
  // else: victim already lives in bucket id, and we still hold that lock.

  victim->dev = dev;
  victim->blockno = blockno;
  victim->valid = 0;
  victim->refcnt = 1;
  release(&bcache.buckets[id].lock);
  release(&bcache.lock);
  acquiresleep(&victim->lock);
  return victim;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Record its free time so LRU eviction can pick the oldest one.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int id = HASH(b->blockno);
  acquire(&bcache.buckets[id].lock);
  b->refcnt--;
  if(b->refcnt == 0){
    b->timestamp = ticks;
  }
  release(&bcache.buckets[id].lock);
}

void
bpin(struct buf *b) {
  int id = HASH(b->blockno);
  acquire(&bcache.buckets[id].lock);
  b->refcnt++;
  release(&bcache.buckets[id].lock);
}

void
bunpin(struct buf *b) {
  int id = HASH(b->blockno);
  acquire(&bcache.buckets[id].lock);
  b->refcnt--;
  release(&bcache.buckets[id].lock);
}
