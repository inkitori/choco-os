#include "timer.h"
#include "io.h"
#include "kprintf.h"
#include "sched.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND 0x43
#define PIT_BASE_HZ 1193182

static volatile uint64_t timer_ticks = 0;

void timer_init(void)
{
	uint16_t divisor = PIT_BASE_HZ / TIMER_HZ;
	outb(PIT_COMMAND, 0x36); // channel 0, lobyte/hibyte, square wave
	outb(PIT_CHANNEL0, divisor & 0xFF);
	outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
	kprintf("timer: PIT at %d Hz\n", TIMER_HZ);
}

void timer_handler(void)
{
	timer_ticks++;
}

uint64_t timer_get_ticks(void)
{
	return timer_ticks;
}

void sleep_ms(uint64_t ms)
{
	uint64_t until = timer_ticks + ms;
	while (timer_ticks < until)
	{
		if (sched_active())
			sched_yield();
		else
			__asm__ volatile("hlt");
	}
}
