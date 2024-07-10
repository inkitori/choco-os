#include "term.h"
#include "framebuffer.h"

static uint8_t line = 0;

void term_init()

{
	framebuffer_init();
	term_clear(0x00000000);
}

void term_clear(uint32_t color)
{
	framebuffer_clear(color);
	line = 0;
}

void term_print(const char *str)
{
	framebuffer_put_string(str, 0, line, TERM_COLOR_WHITE, TERM_COLOR_BLACK);
	line++;
}

void term_print_success(const char *str)
{
	framebuffer_put_string(str, 0, line, TERM_COLOR_GREEN, TERM_COLOR_BLACK);
	line++;
}

void term_print_error(const char *str)
{
	framebuffer_put_string(str, 0, line, TERM_COLOR_RED, TERM_COLOR_BLACK);
	line++;
}