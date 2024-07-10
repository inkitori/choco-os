#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <limine.h>
#include "stdint.h"

void framebuffer_init();
void framebuffer_clear(uint32_t color);
void framebuffer_put_char(unsigned short int c, int cx, int cy, uint32_t fg, uint32_t bg);
void framebuffer_put_string(const char *str, int cx, int cy, uint32_t fg, uint32_t bg);
uint64_t framebuffer_get_width();
uint64_t framebuffer_get_height();
void framebuffer_draw_rect(uint64_t ul_x, uint64_t ul_y, uint64_t width, uint64_t height, uint32_t color);

#endif