#ifndef TERM_H
#define TERM_H

#include <stdint.h>
#include <stdbool.h>

#define TERM_COLOR_BLACK 0x00000000
#define TERM_COLOR_WHITE 0x00FFFFFF
#define TERM_COLOR_RED 0x00FF5555
#define TERM_COLOR_GREEN 0x0050FA7B
#define TERM_COLOR_YELLOW 0x00F1FA8C
#define TERM_COLOR_CYAN 0x008BE9FD
#define TERM_COLOR_MAGENTA 0x00FF79C6
#define TERM_COLOR_GRAY 0x00888888
#define TERM_COLOR_BLUE 0x006272A4

void term_init(void);
void term_clear(uint32_t color);
void term_print(const char *str);
void term_print_with_color(const char *str, uint32_t font_color, uint32_t bg_color);
void term_print_char(char c, uint32_t font_color, uint32_t bg_color);
void term_print_success(const char *str);
void term_print_error(const char *str);

void term_set_colors(uint32_t fg, uint32_t bg);
uint32_t term_get_fg(void);
uint32_t term_get_bg(void);
int term_get_cols(void);
int term_get_rows(void);
int term_get_col(void);
int term_get_row(void);
void term_set_pos(int col, int row);
void term_set_cursor_visible(bool visible);

#endif
