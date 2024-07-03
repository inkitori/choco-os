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
			// size_t fb_index = y * (framebuffer->pitch / sizeof(uint32_t)) + x;
			// uint32_t *fb = (uint32_t *)fb_ptr;

			// fb[fb_index] = color;

			framebuffer_draw_pixel(x, y, color);
		}
	}
}

void framebuffer_draw_pixel(uint64_t x, uint64_t y, uint32_t color)
{
	volatile uint32_t *fb_ptr = framebuffer->address;

	if (x >= framebuffer->width || y >= framebuffer->height)
	{
		return;
	}

	size_t fb_index = y * (framebuffer->pitch / sizeof(uint32_t)) + x;
	uint32_t *fb = (uint32_t *)fb_ptr;

	fb[fb_index] = color;
}

#define PSF1_FONT_MAGIC 0x0436

typedef struct
{
	uint16_t magic;		   // Magic bytes for identification.
	uint8_t fontMode;	   // PSF font mode.
	uint8_t characterSize; // PSF character size.
} PSF1_Header;

#define PSF_FONT_MAGIC 0x864ab572

typedef struct
{
	uint32_t magic;			/* magic bytes to identify PSF */
	uint32_t version;		/* zero */
	uint32_t headersize;	/* offset of bitmaps in file, 32 */
	uint32_t flags;			/* 0 if there's no unicode table */
	uint32_t numglyph;		/* number of glyphs */
	uint32_t bytesperglyph; /* size of each glyph */
	uint32_t height;		/* height in pixels */
	uint32_t width;			/* width in pixels */
} PSF_font;

#define PIXEL uint32_t /* pixel pointer */

extern char _binary_cozette_psf_start;
extern char _binary_cozette_psf_end;

uint16_t *unicode;

void framebuffer_put_char(
	/* note that this is int, not char as it's a unicode character */
	unsigned short int c,
	/* cursor position on screen, in characters not in pixels */
	int cx, int cy,
	/* foreground and background colors, say 0xFFFFFF and 0x000000 */
	uint32_t fg, uint32_t bg)
{
	int scanline = framebuffer->pitch;
	volatile char *fb = framebuffer->address;

	/* cast the address to PSF header struct */
	PSF_font *font = (PSF_font *)&_binary_cozette_psf_start;
	/* we need to know how many bytes encode one row */
	int bytesperline = (font->width + 7) / 8;
	/* unicode translation */
	if (unicode != NULL)
	{
		c = unicode[c];
	}
	/* get the glyph for the character. If there's no
	   glyph for a given character, we'll display the first glyph. */
	unsigned char *glyph =
		(unsigned char *)&_binary_cozette_psf_start +
		font->headersize +
		(c > 0 && c < font->numglyph ? c : 0) * font->bytesperglyph;
	/* calculate the upper left corner on screen where we want to display.
	   we only do this once, and adjust the offset later. This is faster. */
	int offs =
		(cy * font->height * scanline) +
		(cx * (font->width) * sizeof(PIXEL));
	/* finally display pixels according to the bitmap */
	int x, y, line, mask;
	for (y = 0; y < font->height; y++)
	{
		/* save the starting position of the line */
		line = offs;
		mask = 1 << (font->width - 1);
		/* display a row */
		for (x = 0; x < font->width; x++)
		{
			*((PIXEL *)(fb + line)) = *((unsigned int *)glyph) & mask ? fg : bg;
			/* adjust to the next pixel */
			mask >>= 1;
			line += sizeof(PIXEL);
		}
		/* adjust to the next line */
		glyph += bytesperline;
		offs += scanline;
	}
}

void framebuffer_put_string(const char *str, int cx, int cy, uint32_t fg, uint32_t bg)
{
	while (*str)
	{
		framebuffer_put_char(*str, cx, cy, fg, bg);
		cx++;
		str++;
	}
}