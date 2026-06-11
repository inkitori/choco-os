#ifndef KPRINTF_H
#define KPRINTF_H

#include <stdarg.h>
#include <stddef.h>

// Format engine: supports %s %c %d %i %u %x %X %p %% with l/ll/z length
// modifiers, field width, zero-pad and left-justify flags.
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);

// kprintf goes to the serial port (debug log).
void kprintf(const char *fmt, ...);

// term_printf goes to the on-screen terminal AND the serial port.
void term_printf(const char *fmt, ...);

#endif
