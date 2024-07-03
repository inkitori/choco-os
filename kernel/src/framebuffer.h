#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <limine.h>

void framebuffer_init();
void framebuffer_clear(uint32_t color);
static inline void framebuffer_draw_pixel(uint64_t x, uint64_t y, uint32_t color);
void framebuffer_put_char(unsigned short int c, int cx, int cy, uint32_t fg, uint32_t bg);
void framebuffer_put_string(const char *str, int cx, int cy, uint32_t fg, uint32_t bg);

#endif