// Preemptive round-robin scheduler for kernel threads.
//
// A thread's saved context is the interrupt frame the ISR stubs push on its
// own stack: 15 GP registers (pushaq) + RIP/CS/RFLAGS/RSP/SS (iretq frame).
// The timer ISR and the yield software interrupt (0x30) both funnel into
// sched_preempt(rsp) -> new rsp. FPU/SSE state is fxsaved per thread.
#include "sched.h"
#include "malloc.h"
#include "string.h"
#include "kprintf.h"
#include "timer.h"
#include "term.h"

#define STACK_SIZE (64 * 1024)
#define QUANTUM_TICKS 5

#define GDT_KERNEL_CS 0x28
#define GDT_KERNEL_SS 0x30

typedef enum
{
	T_READY,
	T_RUNNING,
	T_SLEEPING,
	T_ZOMBIE,
} ThreadState;

typedef struct Thread
{
	uint8_t fxsave[512] __attribute__((aligned(16)));
	uint64_t rsp;
	int id;
	char name[24];
	ThreadState state;
	uint64_t wake_tick;
	void *stack_base;
	struct Thread *next; // circular ring
} Thread;

static Thread *current = NULL;
static Thread *ring = NULL; // any node in the ring
static bool active = false;
static int next_id = 0;
static uint64_t slice_start = 0;

static inline uint64_t irq_save(void)
{
	uint64_t flags;
	__asm__ volatile("pushfq; pop %0; cli" : "=r"(flags)::"memory");
	return flags;
}

static inline void irq_restore(uint64_t flags)
{
	if (flags & (1 << 9))
		__asm__ volatile("sti");
}

static void fxarea_init(uint8_t *area)
{
	memset(area, 0, 512);
	*(uint16_t *)(area + 0) = 0x037F;	// FCW: default x87 control word
	*(uint32_t *)(area + 24) = 0x1F80;	// MXCSR: mask all SSE exceptions
}

void sched_init(void)
{
	ring = NULL;
	current = NULL;
	active = false;
}

bool sched_active(void)
{
	return active;
}

static void ring_insert(Thread *t)
{
	uint64_t f = irq_save();
	if (!ring)
	{
		ring = t;
		t->next = t;
	}
	else
	{
		t->next = ring->next;
		ring->next = t;
	}
	irq_restore(f);
}

static void trampoline(thread_fn fn, void *arg)
{
	fn(arg);
	thread_exit();
}

int thread_create(const char *name, thread_fn fn, void *arg)
{
	Thread *t = malloc(sizeof(Thread));
	if (!t)
		return -1;
	memset(t, 0, sizeof(Thread));

	t->stack_base = malloc(STACK_SIZE);
	if (!t->stack_base)
	{
		free(t);
		return -1;
	}

	t->id = ++next_id;
	strlcpy(t->name, name, sizeof(t->name));
	t->state = T_READY;
	fxarea_init(t->fxsave);

	// Build an interrupt frame at the top of the stack so the first switch
	// into this thread "returns" into trampoline(fn, arg).
	uint64_t top = ((uint64_t)t->stack_base + STACK_SIZE) & ~0xFull;
	uint64_t *sp = (uint64_t *)top;

	*--sp = GDT_KERNEL_SS;		  // SS
	*--sp = top;				  // RSP
	*--sp = 0x202;				  // RFLAGS (IF set)
	*--sp = GDT_KERNEL_CS;		  // CS
	*--sp = (uint64_t)trampoline; // RIP

	// pushaq order: rax,rbx,rcx,rdx,rbp,rdi,rsi,r8..r15 (r15 ends up lowest)
	*--sp = 0;				// rax
	*--sp = 0;				// rbx
	*--sp = 0;				// rcx
	*--sp = 0;				// rdx
	*--sp = 0;				// rbp
	*--sp = (uint64_t)fn;	// rdi -> trampoline arg 1
	*--sp = (uint64_t)arg;	// rsi -> trampoline arg 2
	for (int i = 0; i < 8; i++)
		*--sp = 0; // r8..r15

	t->rsp = (uint64_t)sp;

	ring_insert(t);
	return t->id;
}

void sched_start(void)
{
	// Adopt the boot context as the idle thread. Its context gets saved into
	// this struct on the first preemption.
	Thread *idle = malloc(sizeof(Thread));
	memset(idle, 0, sizeof(Thread));
	idle->id = 0;
	strcpy(idle->name, "idle");
	idle->state = T_RUNNING;
	fxarea_init(idle->fxsave);
	ring_insert(idle);

	current = idle;
	slice_start = timer_get_ticks();
	active = true;

	for (;;)
		__asm__ volatile("hlt");
}

static void reap_zombies(void)
{
	if (!ring)
		return;
	Thread *p = ring;
	do
	{
		Thread *n = p->next;
		if (n->state == T_ZOMBIE && n != current && n != ring)
		{
			p->next = n->next;
			free(n->stack_base);
			free(n);
		}
		else
		{
			p = p->next;
		}
	} while (p != ring);
}

// Called with interrupts off (from ISR context).
uint64_t sched_preempt(uint64_t rsp)
{
	if (!active || !current)
		return rsp;

	uint64_t now = timer_get_ticks();

	// Wake sleepers.
	Thread *t = current;
	do
	{
		if (t->state == T_SLEEPING && now >= t->wake_tick)
			t->state = T_READY;
		t = t->next;
	} while (t != current);

	reap_zombies();

	// Pick the next runnable thread, preferring anyone but idle. Threads
	// with id 0 (idle) run only when nothing else is ready.
	Thread *next = NULL;
	for (t = current->next;; t = t->next)
	{
		if ((t->state == T_READY || t->state == T_RUNNING) && t->id != 0)
		{
			next = t;
			break;
		}
		if (t == current)
			break;
	}
	if (!next)
	{
		// Fall back to idle (or stay put if current is still runnable).
		for (t = current->next;; t = t->next)
		{
			if (t->state == T_READY || t->state == T_RUNNING)
			{
				next = t;
				break;
			}
			if (t == current)
				break;
		}
	}
	if (!next || next == current)
	{
		if (current->state == T_RUNNING)
			return rsp;
		// current blocked and nobody to run: halt-wait in idle-like loop.
		// (Shouldn't happen since idle is always runnable.)
		return rsp;
	}

	// Context switch.
	current->rsp = rsp;
	if (current->state == T_RUNNING)
		current->state = T_READY;
	__asm__ volatile("fxsave (%0)" ::"r"(current->fxsave) : "memory");

	current = next;
	current->state = T_RUNNING;
	__asm__ volatile("fxrstor (%0)" ::"r"(current->fxsave) : "memory");
	slice_start = now;
	return current->rsp;
}

// Timer hook: only reschedule when the quantum expired.
uint64_t sched_tick(uint64_t rsp)
{
	if (!active)
		return rsp;
	if (timer_get_ticks() - slice_start < QUANTUM_TICKS && current &&
		current->state == T_RUNNING)
		return rsp;
	return sched_preempt(rsp);
}

void sched_yield(void)
{
	if (!active)
		return;
	__asm__ volatile("int $0x30");
}

void thread_exit(void)
{
	uint64_t f = irq_save();
	(void)f;
	current->state = T_ZOMBIE;
	__asm__ volatile("sti; int $0x30");
	for (;;)
		__asm__ volatile("hlt"); // unreachable
}

void thread_sleep_ms(uint64_t ms)
{
	if (!active)
	{
		sleep_ms(ms);
		return;
	}
	uint64_t f = irq_save();
	(void)f;
	current->state = T_SLEEPING;
	current->wake_tick = timer_get_ticks() + ms;
	__asm__ volatile("sti; int $0x30");
}

int sched_thread_count(void)
{
	if (!ring)
		return 0;
	int n = 0;
	Thread *t = ring;
	do
	{
		n++;
		t = t->next;
	} while (t != ring);
	return n;
}

void sched_dump(void)
{
	static const char *state_names[] = {"ready", "running", "sleeping", "zombie"};
	uint64_t f = irq_save();
	Thread *t = current ? current : ring;
	if (!t)
	{
		irq_restore(f);
		term_print("scheduler not started\n");
		return;
	}
	term_printf("  %-4s %-16s %s\n", "ID", "NAME", "STATE");
	Thread *it = t;
	do
	{
		term_printf("  %-4d %-16s %s\n", it->id, it->name,
					state_names[it->state]);
		it = it->next;
	} while (it != t);
	irq_restore(f);
}
