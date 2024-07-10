#include "timer.h"
#include "term.h"
#include "lib.h"

static uint64_t timer_ticks = 0;

void timer_handler()
{
	timer_ticks++;
}

uint64_t timer_get_ticks()
{
	return timer_ticks;
}