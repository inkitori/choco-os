#include "term.h"
#include "framebuffer.h"

static int row = 0;
static int col = 0;

void term_init()

{
	framebuffer_init();
	term_clear(0x00000000);
}

void term_clear(uint32_t color)
{
	framebuffer_clear(color);
	row = 0;
	col = 0;
}

void term_print(char *str)
{
	term_print_with_color(str, TERM_COLOR_WHITE, TERM_COLOR_BLACK);
}

void term_print_with_color(char *str, uint32_t font_color, uint32_t bg_color)
{
	while (*str != '\0')
	{
		term_print_char(*str, font_color, bg_color);
		str++;
	}
}

void term_print_char(char c, uint32_t font_color, uint32_t bg_color)
{
	if (c == '\n')
	{
		row++;
		col = 0;
		int max_rows = framebuffer_get_height() / framebuffer_get_font_height();
		if (row >= max_rows)
		{
			row = 0;
			framebuffer_clear(0x000000);
		}
		return;
	}

	if (c == '\b')
	{
		if (col > 0)
		{
			col--;
			framebuffer_put_char(' ', col, row, font_color, bg_color);
		}
		return;
	}

	int max_cols = framebuffer_get_width() / framebuffer_get_font_width();

	if (col >= max_cols)
	{
		col = 0;
		row++;
	}

	int max_rows = framebuffer_get_height() / framebuffer_get_font_height();
	if (row >= max_rows)
	{
		row = 0;
		framebuffer_clear(0x000000);
	}

	framebuffer_put_char(c, col, row, font_color, bg_color);
	col++;
}

void term_print_success(const char *str)
{
	term_print_with_color((char *)str, TERM_COLOR_GREEN, TERM_COLOR_BLACK);
}

void term_print_error(const char *str)
{
	term_print_with_color((char *)str, TERM_COLOR_RED, TERM_COLOR_BLACK);
}