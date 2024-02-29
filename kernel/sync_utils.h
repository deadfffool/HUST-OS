#ifndef _SYNC_UTILS_H_
#define _SYNC_UTILS_H_
#include "spike_interface/spike_utils.h"


static inline void sync_barrier(volatile int *counter, int all) {

  int local;

  asm volatile("amoadd.w %0, %2, (%1)\n"
               : "=r"(local)
               : "r"(counter), "r"(1)
               : "memory");

  if (local + 1 < all) {
    do {
      asm volatile("lw %0, (%1)\n" : "=r"(local) : "r"(counter) : "memory");
    } while (local < all);
  }
}

typedef struct spin_lock {
  uint64 locked;       // Is the lock held? 
  uint64 cpu;   // The cpu holding the lock.
}spinlock;

static int holding(spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}

static void acquire(spinlock *lk)
{
  intr_off();
  if(holding(lk))
    panic("acquire");
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0);
  __sync_synchronize();
  lk->cpu = mycpu();
}

static void release(spinlock *lk)
{
  if(!holding(lk))
    panic("release");
  __sync_synchronize();
  __sync_lock_release(&lk->locked);
  lk->cpu = 0;
  intr_on();
}

#endif