#include "shell.h"
#include "term.h"
#include "keyboard.h"

#include "stdint.h"

void shell_init()
{
	char buf[3];
	while (1)
	{
		char c = keyboard_getch();
		if (c == 0)
			continue;
		buf[0] = c;
		buf[1] = '\n';
		buf[2] = '\0';
		term_print(buf);
	}
}