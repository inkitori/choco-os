#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "asm_wrappers.h"
#include "framebuffer.h"

__attribute__((used, section(".requests"))) static volatile struct limine_framebuffer_request framebuffer_request = {
	.id = LIMINE_FRAMEBUFFER_REQUEST,
	.revision = 0};

struct limine_framebuffer *framebuffer;

void framebuffer_init()
{
	// Ensure we got a framebuffer.
	if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1)
	{
		hcf();
	}

	framebuffer = framebuffer_request.response->framebuffers[0];
}

void framebuffer_clear(uint32_t color)
{
	volatile uint32_t *fb_ptr = framebuffer->address;

	for (size_t x = 0; x < framebuffer->width; x++)
	{
		for (size_t y = 0; y < framebuffer->height; y++)
		{
			// fb_ptr[x + y * framebuffer->width] = color;
			size_t fb_index = y * (framebuffer->pitch / sizeof(uint32_t)) + x;
			uint32_t *fb = (uint32_t *)fb_ptr;

			fb[fb_index] = color;
		}
	}
}