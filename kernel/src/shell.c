#include "shell.h"
#include "term.h"
#include "keyboard.h"
#include "lib.h"
#include "snake.h"

#include "stdint.h"

static void shell_clear()
{
	term_clear(0x000000);
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

static void process_command(char *command)
{
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

		char c = keyboard_key_to_char(key);
		buf[buf_index] = c;
		buf_index++;

		term_print_char(c, 0xFFFFFF, 0x000000);
	}
}