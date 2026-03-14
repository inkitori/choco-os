#include "shell.h"
#include "term.h"
#include "keyboard.h"
#include "lib.h"
#include "snake.h"
#include "mem.h"
#include "malloc.h"

#include "stdint.h"

static void shell_clear()
{
	term_clear(0x000000);
	term_print(">");
}

static void handle_fetch()
{
	uint32_t brown = 0x8b4513;
	uint32_t key_color = 0xe80c5c;
	uint32_t val_color = TERM_COLOR_WHITE;

	term_print("\n");
	term_print_with_color("    /\\_/\\       ", brown, TERM_COLOR_BLACK);
	term_print_with_color("OS: ", key_color, TERM_COLOR_BLACK);
	term_print_with_color("Choco OS\n", val_color, TERM_COLOR_BLACK);

	term_print_with_color("   ( o.o )      ", brown, TERM_COLOR_BLACK);
	term_print_with_color("Kernel: ", key_color, TERM_COLOR_BLACK);
	term_print_with_color("Choco Kernel\n", val_color, TERM_COLOR_BLACK);

	term_print_with_color("    > ^ <       ", brown, TERM_COLOR_BLACK);
	term_print_with_color("Author: ", key_color, TERM_COLOR_BLACK);
	term_print_with_color("ink\n\n", val_color, TERM_COLOR_BLACK);

	term_print(">");
}

static void handle_ping()
{
	term_print("pong\n");
	term_print(">");
}

static void handle_snake()
{
	snake_init();
	shell_clear();
}

static void handle_clear()
{
	shell_clear();
}

static void handle_memmap()
{
	debug_memmap();
	term_print(">");
}

static void handle_help()
{
	term_print("Available commands:\n");
	term_print("  help       - Print this help message\n");
	term_print("  fetch      - Print fetch\n");
	term_print("  ping       - Ping\n");
	term_print("  snake      - Play Snake\n");
	term_print("  clear      - Clear terminal\n");
	term_print("  memmap     - Display memory map\n");
	term_print("  testmalloc - Test memory alloc\n");
	term_print(">");
}


static void process_command(char *command)
{
	if (cmp_string(command, "help"))
	{
		handle_help();
		return;
	}
	if (cmp_string(command, "ping"))
	{
		handle_ping();
		return;
	}
	if (cmp_string(command, "snake"))
	{
		handle_snake();
		return;
	}
	if (cmp_string(command, "clear"))
	{
		handle_clear();
		return;
	}
	if (cmp_string(command, "fetch"))
	{
		handle_fetch();
		return;
	}
	if (cmp_string(command, "memmap"))
	{
		handle_memmap();
		return;
	}
	if (cmp_string(command, "testmalloc"))
	{
		test_malloc();
		term_print(">");
		return;
	}
	term_print(">");
}

void shell_init()
{
	char buf[128];
	uint8_t buf_index = 0;

	shell_clear();
	while (1)
	{
		Key key = keyboard_get_key();
		if (key == NONE)
		{
			continue;
		}
		if (key == ENTER)
		{
			term_print("\n");

			buf[buf_index] = '\0';
			process_command(buf);

			buf_index = 0;

			continue;
		}

		if (key == BACKSPACE)
		{
			if (buf_index > 0)
			{
				buf_index--;
				term_print_char('\b', 0xFFFFFF, 0x000000);
			}
			continue;
		}

		char c = keyboard_key_to_char(key);
		if (c != '\0' && buf_index < sizeof(buf) - 1)
		{
			buf[buf_index] = c;
			buf_index++;

			term_print_char(c, 0xFFFFFF, 0x000000);
		}
	}
}