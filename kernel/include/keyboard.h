#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

// Legacy key enum kept for the games (snake/pong).
typedef enum Key
{
	A, B, C, D, E, F, G, H, I, J, K, L, M,
	N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
	ENTER,
	SPACE,
	ESCAPE,
	BACKSPACE,
	NONE,
	KEY_COUNT
} Key;

// Special (non-ASCII) key codes delivered through kbd events.
typedef enum
{
	KEY_NONE = 0,
	KEY_CHAR,
	KEY_UP,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_HOME,
	KEY_END,
	KEY_PGUP,
	KEY_PGDN,
	KEY_DELETE,
	KEY_ESCAPE,
	KEY_TAB,
	KEY_F1, KEY_F2, KEY_F3, KEY_F4,
} KeyCode;

typedef struct
{
	KeyCode code; // KEY_CHAR for printable chars (incl \n, \b)
	char ch;	  // valid when code == KEY_CHAR
	bool ctrl;
	bool alt;
} KeyEvent;

void keyboard_init(void);
void keyboard_handler(void);

// Event API: returns false if no event pending.
bool kbd_poll_event(KeyEvent *ev);
// Blocking: halts (or yields, once the scheduler runs) until an event arrives.
KeyEvent kbd_wait_event(void);
// Convenience: blocking read of next printable char/\n/\b. Arrows etc. are skipped.
char kbd_getchar(void);
// Drop any pending input.
void kbd_flush(void);

// Legacy API for the games.
Key keyboard_get_key(void);
char keyboard_key_to_char(Key key);

#endif
