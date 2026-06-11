#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#define TIMER_HZ 1000

void timer_init(void);
void timer_handler(void);
uint64_t timer_get_ticks(void); // milliseconds since boot
void sleep_ms(uint64_t ms);

#endif
