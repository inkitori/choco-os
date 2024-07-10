#ifndef TERM_H
#define TERM_H

#include "stdint.h"

#define TERM_COLOR_BLACK 0x00000000
#define TERM_COLOR_WHITE 0xFFFFFFFF
#define TERM_COLOR_RED 0xFF0000
#define TERM_COLOR_GREEN 0x00FF00

void term_init();
void term_clear(uint32_t color);
void term_print(char *str);
void term_print_char(char c, uint32_t font_color, uint32_t bg_color);
void term_print_success(const char *str);
void term_print_error(const char *str);

#endif