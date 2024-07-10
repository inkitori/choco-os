#include "term.h"
#include "framebuffer.h"

static uint8_t row = 0;
static uint8_t col = 0;

void term_init()

{
	framebuffer_init();
	term_clear(0x00000000);
}

void term_clear(uint32_t color)
{
	framebuffer_clear(color);
	row = 0;
}

void term_print(char *str)
{
	while (*str != '\0')
	{
		term_print_char(*str, TERM_COLOR_WHITE, TERM_COLOR_BLACK);
		str++;
	}

	row++;
}

void term_print_char(char c, uint32_t font_color, uint32_t bg_color)
{
	if (c == '\n')
	{
		row++;
		col = 0;
		return;
	}

	framebuffer_put_char(c, col, row, font_color, bg_color);
	col++;
}

void term_print_success(const char *str)
{
	return;
	framebuffer_put_string(str, 0, row, TERM_COLOR_GREEN, TERM_COLOR_BLACK);
	row++;
}

void term_print_error(const char *str)
{
	return;
	framebuffer_put_string(str, 0, row, TERM_COLOR_RED, TERM_COLOR_BLACK);
	row++;
}