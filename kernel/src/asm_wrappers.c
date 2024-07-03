#include "asm_wrappers.h"

void hcf(void)
{
	asm("cli");
	for (;;)
	{
		asm("hlt");
	}
}
