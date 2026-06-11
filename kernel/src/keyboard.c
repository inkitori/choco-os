// PS/2 keyboard driver, scancode set 2 (translation disabled).
// Builds KeyEvents (printable chars + special keys + modifiers) from the
// raw byte stream, including E0-extended and F0-break sequences.
#include "keyboard.h"
#include "ps2.h"
#include "io.h"
#include "kprintf.h"
#include "sched.h"

#include <stdint.h>
#include <stdbool.h>

#define KEYBOARD_ENABLE_SCANNING 0xF4
#define KEYBOARD_SCAN_CODE_SET 0xF0
#define KEYBOARD_SET_SCAN_CODE_2 0x02

#define SC_RELEASE 0xF0
#define SC_EXTENDED 0xE0

#define SC_LSHIFT 0x12
#define SC_RSHIFT 0x59
#define SC_LCTRL 0x14
#define SC_LALT 0x11
#define SC_CAPSLOCK 0x58

#define EVENT_QUEUE_SIZE 128

// Scancode set 2 -> ASCII, unshifted and shifted. Index is the scancode.
static const char sc2_ascii[0x80] = {
	[0x0D] = '\t', [0x0E] = '`',
	[0x15] = 'q', [0x16] = '1', [0x1A] = 'z', [0x1B] = 's', [0x1C] = 'a',
	[0x1D] = 'w', [0x1E] = '2', [0x21] = 'c', [0x22] = 'x', [0x23] = 'd',
	[0x24] = 'e', [0x25] = '4', [0x26] = '3', [0x29] = ' ', [0x2A] = 'v',
	[0x2B] = 'f', [0x2C] = 't', [0x2D] = 'r', [0x2E] = '5', [0x31] = 'n',
	[0x32] = 'b', [0x33] = 'h', [0x34] = 'g', [0x35] = 'y', [0x36] = '6',
	[0x3A] = 'm', [0x3B] = 'j', [0x3C] = 'u', [0x3D] = '7', [0x3E] = '8',
	[0x41] = ',', [0x42] = 'k', [0x43] = 'i', [0x44] = 'o', [0x45] = '0',
	[0x46] = '9', [0x49] = '.', [0x4A] = '/', [0x4B] = 'l', [0x4C] = ';',
	[0x4D] = 'p', [0x4E] = '-', [0x52] = '\'', [0x54] = '[', [0x55] = '=',
	[0x5A] = '\n', [0x5B] = ']', [0x5D] = '\\', [0x66] = '\b',
};

static const char sc2_ascii_shift[0x80] = {
	[0x0D] = '\t', [0x0E] = '~',
	[0x15] = 'Q', [0x16] = '!', [0x1A] = 'Z', [0x1B] = 'S', [0x1C] = 'A',
	[0x1D] = 'W', [0x1E] = '@', [0x21] = 'C', [0x22] = 'X', [0x23] = 'D',
	[0x24] = 'E', [0x25] = '$', [0x26] = '#', [0x29] = ' ', [0x2A] = 'V',
	[0x2B] = 'F', [0x2C] = 'T', [0x2D] = 'R', [0x2E] = '%', [0x31] = 'N',
	[0x32] = 'B', [0x33] = 'H', [0x34] = 'G', [0x35] = 'Y', [0x36] = '^',
	[0x3A] = 'M', [0x3B] = 'J', [0x3C] = 'U', [0x3D] = '&', [0x3E] = '*',
	[0x41] = '<', [0x42] = 'K', [0x43] = 'I', [0x44] = 'O', [0x45] = ')',
	[0x46] = '(', [0x49] = '>', [0x4A] = '?', [0x4B] = 'L', [0x4C] = ':',
	[0x4D] = 'P', [0x4E] = '_', [0x52] = '"', [0x54] = '{', [0x55] = '+',
	[0x5A] = '\n', [0x5B] = '}', [0x5D] = '|', [0x66] = '\b',
};

static KeyEvent event_queue[EVENT_QUEUE_SIZE];
static volatile uint32_t eq_read = 0;
static volatile uint32_t eq_write = 0;

static bool shift_down = false;
static bool ctrl_down = false;
static bool alt_down = false;
static bool caps_on = false;

static bool seen_release = false;
static bool seen_extended = false;

void keyboard_init(void)
{
	ps2_data_out(KEYBOARD_ENABLE_SCANNING);
	ps2_data_in(); // ack

	ps2_data_out(KEYBOARD_SCAN_CODE_SET);
	ps2_data_in();
	ps2_data_out(KEYBOARD_SET_SCAN_CODE_2);
	ps2_data_in();

	kprintf("keyboard: scancode set 2 ready\n");
}

static void push_event(KeyEvent ev)
{
	uint32_t next = (eq_write + 1) % EVENT_QUEUE_SIZE;
	if (next == eq_read)
		return; // queue full, drop
	event_queue[eq_write] = ev;
	eq_write = next;
}

static KeyCode extended_keycode(uint8_t sc)
{
	switch (sc)
	{
	case 0x75: return KEY_UP;
	case 0x72: return KEY_DOWN;
	case 0x6B: return KEY_LEFT;
	case 0x74: return KEY_RIGHT;
	case 0x6C: return KEY_HOME;
	case 0x69: return KEY_END;
	case 0x7D: return KEY_PGUP;
	case 0x7A: return KEY_PGDN;
	case 0x71: return KEY_DELETE;
	default: return KEY_NONE;
	}
}

void keyboard_handler(void)
{
	uint8_t sc = inb(0x60);

	if (sc == SC_EXTENDED)
	{
		seen_extended = true;
		return;
	}
	if (sc == SC_RELEASE)
	{
		seen_release = true;
		return;
	}

	bool ext = seen_extended;
	bool release = seen_release;
	seen_extended = false;
	seen_release = false;

	// Modifier tracking (E0 14 is right-ctrl, E0 11 is right-alt).
	if (!ext && (sc == SC_LSHIFT || sc == SC_RSHIFT))
	{
		shift_down = !release;
		return;
	}
	if (sc == SC_LCTRL)
	{
		ctrl_down = !release;
		return;
	}
	if (sc == SC_LALT)
	{
		alt_down = !release;
		return;
	}
	if (!ext && sc == SC_CAPSLOCK)
	{
		if (!release)
			caps_on = !caps_on;
		return;
	}

	if (release)
		return;

	KeyEvent ev = {.code = KEY_NONE, .ch = 0, .ctrl = ctrl_down, .alt = alt_down};

	if (ext)
	{
		ev.code = extended_keycode(sc);
		if (ev.code == KEY_NONE)
			return;
		push_event(ev);
		return;
	}

	switch (sc)
	{
	case 0x76: ev.code = KEY_ESCAPE; push_event(ev); return;
	case 0x05: ev.code = KEY_F1; push_event(ev); return;
	case 0x06: ev.code = KEY_F2; push_event(ev); return;
	case 0x04: ev.code = KEY_F3; push_event(ev); return;
	case 0x0C: ev.code = KEY_F4; push_event(ev); return;
	}

	if (sc >= 0x80)
		return;

	char c = shift_down ? sc2_ascii_shift[sc] : sc2_ascii[sc];
	if (c == 0)
		return;

	if (caps_on && !shift_down && c >= 'a' && c <= 'z')
		c = c - 'a' + 'A';
	else if (caps_on && shift_down && c >= 'A' && c <= 'Z')
		c = c - 'A' + 'a';

	ev.code = KEY_CHAR;
	ev.ch = c;
	push_event(ev);
}

bool kbd_poll_event(KeyEvent *ev)
{
	if (eq_read == eq_write)
		return false;
	*ev = event_queue[eq_read];
	eq_read = (eq_read + 1) % EVENT_QUEUE_SIZE;
	return true;
}

KeyEvent kbd_wait_event(void)
{
	KeyEvent ev;
	while (!kbd_poll_event(&ev))
	{
		if (sched_active())
			sched_yield();
		else
			__asm__ volatile("hlt");
	}
	return ev;
}

char kbd_getchar(void)
{
	for (;;)
	{
		KeyEvent ev = kbd_wait_event();
		if (ev.code == KEY_CHAR)
			return ev.ch;
	}
}

void kbd_flush(void)
{
	eq_read = eq_write;
}

// ---- Legacy API for the games ----

Key keyboard_get_key(void)
{
	KeyEvent ev;
	if (!kbd_poll_event(&ev))
		return NONE;

	if (ev.code == KEY_ESCAPE)
		return ESCAPE;
	if (ev.code != KEY_CHAR)
		return NONE;

	char c = ev.ch;
	if (c >= 'A' && c <= 'Z')
		c = c - 'A' + 'a';
	if (c >= 'a' && c <= 'z')
		return (Key)(A + (c - 'a'));

	switch (c)
	{
	case '\n': return ENTER;
	case ' ': return SPACE;
	case '\b': return BACKSPACE;
	default: return NONE;
	}
}

char keyboard_key_to_char(Key key)
{
	if (key >= A && key <= Z)
		return 'a' + (key - A);
	switch (key)
	{
	case SPACE: return ' ';
	case BACKSPACE: return '\b';
	default: return '\0';
	}
}
