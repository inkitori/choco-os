// Scrolling framebuffer terminal with a block cursor. Output is mirrored
// to the serial port so the console is scriptable from the host.
#include "term.h"
#include "framebuffer.h"
#include "string.h"
#include "serial.h"

static int row = 0;
static int col = 0;
static int cols = 0;
static int rows = 0;
static uint32_t cur_fg = TERM_COLOR_WHITE;
static uint32_t cur_bg = TERM_COLOR_BLACK;
static bool cursor_visible = true;
static bool cursor_drawn = false;

static void draw_cursor(bool on)
{
	if (!cursor_visible && on)
		return;
	uint32_t fw = framebuffer_get_font_width();
	uint32_t fh = framebuffer_get_font_height();
	framebuffer_draw_rect(col * fw, row * fh + fh - 2, fw, 2,
						  on ? cur_fg : cur_bg);
	cursor_drawn = on;
}

void term_init(void)
{
	framebuffer_init();
	cols = framebuffer_get_width() / framebuffer_get_font_width();
	rows = framebuffer_get_height() / framebuffer_get_font_height();
	term_clear(TERM_COLOR_BLACK);
}

void term_clear(uint32_t color)
{
	framebuffer_clear(color);
	row = 0;
	col = 0;
	cursor_drawn = false;
}

void term_set_colors(uint32_t fg, uint32_t bg)
{
	cur_fg = fg;
	cur_bg = bg;
}

uint32_t term_get_fg(void) { return cur_fg; }
uint32_t term_get_bg(void) { return cur_bg; }
int term_get_cols(void) { return cols; }
int term_get_rows(void) { return rows; }
int term_get_col(void) { return col; }
int term_get_row(void) { return row; }

void term_set_pos(int c, int r)
{
	if (cursor_drawn)
		draw_cursor(false);
	if (c >= 0 && c < cols)
		col = c;
	if (r >= 0 && r < rows)
		row = r;
	draw_cursor(true);
}

void term_set_cursor_visible(bool visible)
{
	if (!visible && cursor_drawn)
		draw_cursor(false);
	cursor_visible = visible;
	if (visible)
		draw_cursor(true);
}

static void newline(void)
{
	col = 0;
	row++;
	if (row >= rows)
	{
		framebuffer_scroll_up(framebuffer_get_font_height(), TERM_COLOR_BLACK);
		row = rows - 1;
	}
}

void term_print_char(char c, uint32_t font_color, uint32_t bg_color)
{
	if (c == '\n')
		serial_putchar('\r');
	serial_putchar(c);

	if (cursor_drawn)
		draw_cursor(false);

	switch (c)
	{
	case '\n':
		newline();
		break;
	case '\r':
		col = 0;
		break;
	case '\t':
		do
		{
			term_print_char(' ', font_color, bg_color);
		} while (col % 4 != 0);
		return; // cursor already redrawn by recursive call
	case '\b':
		if (col > 0)
		{
			col--;
			framebuffer_put_char(' ', col, row, font_color, bg_color);
		}
		break;
	default:
		framebuffer_put_char((unsigned char)c, col, row, font_color, bg_color);
		col++;
		if (col >= cols)
			newline();
		break;
	}

	draw_cursor(true);
}

void term_print_with_color(const char *str, uint32_t font_color, uint32_t bg_color)
{
	while (*str)
		term_print_char(*str++, font_color, bg_color);
}

void term_print(const char *str)
{
	term_print_with_color(str, cur_fg, cur_bg);
}

void term_print_success(const char *str)
{
	term_print_with_color(str, TERM_COLOR_GREEN, TERM_COLOR_BLACK);
}

void term_print_error(const char *str)
{
	term_print_with_color(str, TERM_COLOR_RED, TERM_COLOR_BLACK);
}
