#include "shell.h"
#include "term.h"
#include "keyboard.h"
#include "lib.h"
#include "snake.h"
#include "pong.h"
#include "mem.h"
#include "malloc.h"
#include "pmm.h"
#include "vmm.h"

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

static void handle_pong()
{
	pong_init();
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

static void handle_testpmm()
{
	term_print("Testing PMM...\n");
	
	// Test single page allocation
	void* page1 = pmm_alloc_page();
	if (page1) {
		term_print_success("PASS: Single page allocated at ");
		char buf[64];
		to_string((uint64_t)page1, buf);
		term_print(buf);
		term_print("\n");
	} else {
		term_print_error("FAIL: Single page allocation failed\n");
	}
	
	// Test multiple page allocation
	void* page2 = pmm_alloc_pages(4);
	if (page2) {
		term_print_success("PASS: 4 pages allocated at ");
		char buf[64];
		to_string((uint64_t)page2, buf);
		term_print(buf);
		term_print("\n");
	} else {
		term_print_error("FAIL: 4 pages allocation failed\n");
	}
	
	// Test free
	if (page1) pmm_free_page(page1);
	if (page2) pmm_free_pages(page2, 4);
	
	term_print_success("PMM test completed.\n");
	term_print(">");
}

static void handle_testvmm()
{
	term_print("Testing VMM...\n");
	
	uint64_t* pml4 = vmm_get_kernel_pml4();
	if (pml4) {
		term_print_success("PASS: Got kernel PML4 at ");
		char buf[64];
		to_string((uint64_t)pml4, buf);
		term_print(buf);
		term_print("\n");
	} else {
		term_print_error("FAIL: Failed to get kernel PML4\n");
	}
	
	// Test mapping
	void* phys_page = pmm_alloc_page();
	if (phys_page) {
		uint64_t test_vaddr = 0x1000000000; // Arbitrary high virtual address
		
		vmm_map_page(pml4, test_vaddr, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE);
		
		uint64_t mapped_phys = vmm_get_phys(pml4, test_vaddr);
		if (mapped_phys == (uint64_t)phys_page) {
			term_print_success("PASS: Page mapping successful\n");
			
			// Test unmapping
			vmm_unmap_page(pml4, test_vaddr);
			mapped_phys = vmm_get_phys(pml4, test_vaddr);
			if (mapped_phys == 0) {
				term_print_success("PASS: Page unmapping successful\n");
			} else {
				term_print_error("FAIL: Page still mapped after unmap\n");
			}
		} else {
			term_print_error("FAIL: Physical address mismatch after mapping\n");
		}
		
		pmm_free_page(phys_page);
	} else {
		term_print_error("FAIL: Could not allocate physical page for VMM test\n");
	}
	
	term_print_success("VMM test completed.\n");
	term_print(">");
}

static void handle_help()
{
	term_print("Available commands:\n");
	term_print("  help       - Print this help message\n");
	term_print("  fetch      - Print fetch\n");
	term_print("  ping       - Ping\n");
	term_print("  snake      - Play Snake\n");
	term_print("  pong       - Play Pong\n");
	term_print("  clear      - Clear terminal\n");
	term_print("  memmap     - Display memory map\n");
	term_print("  testmalloc - Test memory alloc\n");
	term_print("  testpmm    - Test physical memory manager\n");
	term_print("  testvmm    - Test virtual memory manager\n");
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
	if (cmp_string(command, "pong"))
	{
		handle_pong();
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
	if (cmp_string(command, "testpmm"))
	{
		handle_testpmm();
		return;
	}
	if (cmp_string(command, "testvmm"))
	{
		handle_testvmm();
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