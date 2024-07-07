#include "shell.h"
#include "term.h"
#include "keyboard.h"

#include "stdint.h"

void shell_init()
{
	// term_clear(0x000000);

	while (1)
	{
		term_print(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
		char c = keyboard_getchar();
		term_print("You pressed: ");
		// term_print(&c);
	}
}