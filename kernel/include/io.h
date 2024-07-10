#ifndef IO_H
#define IO_H

#include "stdint.h"

// TODO: CHECK IF INLINE IS VALID FOR GLOBAL FUNCTIONS

uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t val);
void io_wait(void);

#endif