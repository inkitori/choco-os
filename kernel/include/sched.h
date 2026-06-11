#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*thread_fn)(void *arg);

void sched_init(void);
// Turn the calling context into the first thread and enable preemption.
void sched_start(void);
bool sched_active(void);

int thread_create(const char *name, thread_fn fn, void *arg);
void sched_yield(void);
void thread_exit(void);
void thread_sleep_ms(uint64_t ms);

// Called from the timer interrupt with the interrupted context's stack
// pointer; returns the stack pointer to resume (may belong to another thread).
uint64_t sched_preempt(uint64_t rsp);
// Like sched_preempt but only switches when the time slice expired.
uint64_t sched_tick(uint64_t rsp);

int sched_thread_count(void);
void sched_dump(void); // "ps"

#endif
