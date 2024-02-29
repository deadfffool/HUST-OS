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

typedef struct {
  volatile uint64 lock;
} spinlock;

static void spinlock_init(spinlock *lock) {
    lock->lock = 0;
}

static void acquire(spinlock *lk)
{
  intr_off();
  uint64 value;
  do{
    asm volatile("amoswap.w.aq %0, %1, (%2)"
                          : "=r"(value)
                          : "r"(1), "r"(&(lk->lock))
                          :);
  } while (value != 0);
}

static void release(spinlock *lk)
{
  asm volatile("amoswap.w.rl x0, %0, (%1)"
                      :
                      : "r"(0), "r"(&(lk->lock))
                      :);
  intr_on();
}

#endif