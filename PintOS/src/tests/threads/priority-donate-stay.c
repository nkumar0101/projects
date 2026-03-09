#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

#define NUM_HIGH_PRIO_THREADS 2

static thread_func high_prio_thread_func;

void test_priority_donate_stay(void) {
  struct lock a, b;

  /* This test does not work with the MLFQS. */
  ASSERT(active_sched_policy == SCHED_PRIO);

  /* Make sure our priority is the default. */
  ASSERT(thread_get_priority() == PRI_DEFAULT);

  lock_init(&a);
  lock_init(&b);

  // Acquire both locks
  lock_acquire(&a);
  lock_acquire(&b);
  msg("Main thread acquired locks A and B.");

  // Create multiple higher-priority threads, each targeting one of the locks
  for (int i = 0; i < NUM_HIGH_PRIO_THREADS; ++i) {
    char name[16];
    snprintf(name, sizeof name, "high%d", i);
    thread_create(name, PRI_DEFAULT + 10, high_prio_thread_func, i == 0 ? &a : &b);
  }

  // Release locks in sequence
  lock_release(&b);
  msg("Main thread released lock B.");

  msg("Main thread should print this before high1 acts.");

  lock_release(&a);
  msg("Main thread released lock A.");

  msg("Main thread should print this after high0 acts.");
}

static void high_prio_thread_func(void* lock_) {
  struct lock* lock = lock_;

  lock_acquire(lock);
  msg("High priority thread %s acquired lock.", thread_name());
  lock_release(lock);
  msg("High priority thread %s released lock.", thread_name());
}