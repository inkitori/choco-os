// ChocoCord: a Discord-style chat TUI rendered straight on the framebuffer.
// Each channel is backed by a bot persona running on the in-kernel LLM;
// replies generate in a background thread and stream in live.
#include "chat.h"
#include "framebuffer.h"
#include "keyboard.h"
#include "string.h"
#include "kprintf.h"
#include "malloc.h"
#include "timer.h"
#include "sched.h"
#include "llm.h"

#include <stdint.h>
#include <stdbool.h>

// Discord dark theme palette
#define COL_SERVERBAR 0x202225
#define COL_SIDEBAR 0x2F3136
#define COL_CHAT 0x36393F
#define COL_INPUT 0x40444B
#define COL_TEXT 0xDCDDDE
#define COL_MUTED 0x72767D
#define COL_HEADER 0xFFFFFF
#define COL_BLURPLE 0x5865F2
#define COL_GREEN 0x3BA55D
#define COL_SELECTED 0x42464D
#define COL_DIVIDER 0x26282C

#define MSG_TEXT_MAX 2048
#define MSGS_PER_CHANNEL 24
#define INPUT_MAX 220
#define NUM_CHANNELS 3

typedef struct
{
	char author[16];
	uint32_t color;
	char text[MSG_TEXT_MAX];
	uint32_t minutes; // uptime minutes when sent
} Message;

typedef struct
{
	const char *name;	  // "#stories"
	const char *topic;
	const char *bot;	  // bot username
	uint32_t bot_color;
	const char *model;
	int temp_centi;
	int topp_centi;
	int max_tokens;
	Message msgs[MSGS_PER_CHANNEL];
	int msg_count; // total ever; ring index = count % MSGS_PER_CHANNEL
	volatile bool dirty;
	volatile bool typing;
} Channel;

static Channel channels[NUM_CHANNELS] = {
	{.name = "# stories",
	 .topic = "tell chocobot the start of a story (stories15M)",
	 .bot = "chocobot",
	 .bot_color = 0xF1C40F,
	 .model = "stories15M",
	 .temp_centi = 80,
	 .topp_centi = 90,
	 .max_tokens = 180},
	{.name = "# speedrun",
	 .topic = "turbobot answers instantly (stories260K)",
	 .bot = "turbobot",
	 .bot_color = 0x2ECC71,
	 .model = "stories260K",
	 .temp_centi = 80,
	 .topp_centi = 90,
	 .max_tokens = 200},
	{.name = "# chaos",
	 .topic = "chaosbot runs hot, temperature 1.4 (stories260K)",
	 .bot = "chaosbot",
	 .bot_color = 0xE74C3C,
	 .model = "stories260K",
	 .temp_centi = 140,
	 .topp_centi = 98,
	 .max_tokens = 200},
};

static int current_chan = 0;
static char input[INPUT_MAX];
static int input_len = 0;
static volatile bool running = false;

// layout (in character cells)
static int fw, fh, cols, rows;
static int sidebar_cols, members_cols, chat_x, chat_cols;
static int header_rows = 2, input_rows = 2;
static int chat_top, chat_rows;

// ---------- drawing helpers ----------

static void fill_cells(int cx, int cy, int w, int h, uint32_t color)
{
	framebuffer_draw_rect((uint64_t)cx * fw, (uint64_t)cy * fh,
						  (uint64_t)w * fw, (uint64_t)h * fh, color);
}

static void put_text(const char *s, int cx, int cy, uint32_t fg, uint32_t bg)
{
	while (*s)
	{
		framebuffer_put_char((unsigned char)*s, cx++, cy, fg, bg);
		s++;
	}
}

static void put_text_n(const char *s, int n, int cx, int cy, uint32_t fg,
					   uint32_t bg)
{
	for (int i = 0; i < n && s[i]; i++)
		framebuffer_put_char((unsigned char)s[i], cx + i, cy, fg, bg);
}

// ---------- panels ----------

static void draw_serverbar(void)
{
	fill_cells(0, 0, 3, rows, COL_SERVERBAR);
	// a lone "server icon"
	framebuffer_draw_rect(fw / 2, fh, 2 * fw, 2 * fh, COL_BLURPLE);
	put_text("C", 1, 1, 0xFFFFFF, COL_BLURPLE);
}

static void draw_sidebar(void)
{
	fill_cells(3, 0, sidebar_cols, rows, COL_SIDEBAR);
	put_text("Choco Cord", 5, 1, COL_HEADER, COL_SIDEBAR);
	fill_cells(3, 2, sidebar_cols, 1, COL_DIVIDER);
	put_text("TEXT CHANNELS", 5, 4, COL_MUTED, COL_SIDEBAR);

	for (int i = 0; i < NUM_CHANNELS; i++)
	{
		uint32_t bg = (i == current_chan) ? COL_SELECTED : COL_SIDEBAR;
		uint32_t fg = (i == current_chan) ? 0xFFFFFF : COL_MUTED;
		fill_cells(4, 6 + i * 2, sidebar_cols - 2, 1, bg);
		put_text(channels[i].name, 5, 6 + i * 2, fg, bg);
	}

	put_text("[tab] switch", 5, rows - 4, COL_MUTED, COL_SIDEBAR);
	put_text("[esc] exit", 5, rows - 3, COL_MUTED, COL_SIDEBAR);

	// "user pill" at the bottom like Discord
	fill_cells(3, rows - 2, sidebar_cols, 2, COL_SERVERBAR);
	put_text("you", 5, rows - 1, COL_TEXT, COL_SERVERBAR);
	put_text("#0001", 9, rows - 1, COL_MUTED, COL_SERVERBAR);
}

static void draw_header(void)
{
	Channel *ch = &channels[current_chan];
	fill_cells(chat_x, 0, cols - chat_x, header_rows, COL_CHAT);
	put_text(ch->name, chat_x + 1, 0, COL_HEADER, COL_CHAT);
	int off = chat_x + 1 + (int)strlen(ch->name) + 2;
	put_text(ch->topic, off, 0, COL_MUTED, COL_CHAT);
	fill_cells(chat_x, 1, cols - chat_x, 1, COL_DIVIDER);
}

static void draw_members(void)
{
	int x = cols - members_cols;
	fill_cells(x, header_rows, members_cols, rows - header_rows, COL_SIDEBAR);
	put_text("ONLINE - 4", x + 2, header_rows + 1, COL_MUTED, COL_SIDEBAR);

	const char *names[4] = {"you", channels[0].bot, channels[1].bot,
							channels[2].bot};
	uint32_t colors[4] = {COL_BLURPLE, channels[0].bot_color,
						  channels[1].bot_color, channels[2].bot_color};
	for (int i = 0; i < 4; i++)
	{
		int y = header_rows + 3 + i * 2;
		framebuffer_draw_rect((uint64_t)(x + 2) * fw,
							  (uint64_t)y * fh + fh / 4, fw / 2, fw / 2,
							  COL_GREEN);
		put_text(names[i], x + 4, y, colors[i], COL_SIDEBAR);
	}
}

// Word-wrap one message text into the line buffer.
#define MAX_LINES 512
typedef struct
{
	char text[200];
	uint32_t fg;
	bool author_line;
	uint32_t author_color;
	uint32_t minutes;
} WrapLine;

static WrapLine *lines; // MAX_LINES, heap-allocated once
static int line_count;

static void wrap_push(const char *s, int n, uint32_t fg)
{
	if (line_count >= MAX_LINES)
		return;
	WrapLine *L = &lines[line_count++];
	if (n > (int)sizeof(L->text) - 1)
		n = sizeof(L->text) - 1;
	memcpy(L->text, s, n);
	L->text[n] = '\0';
	L->fg = fg;
	L->author_line = false;
}

static void wrap_message(Message *m, int width)
{
	if (line_count >= MAX_LINES)
		return;

	// author header line
	WrapLine *L = &lines[line_count++];
	strlcpy(L->text, m->author, sizeof(L->text));
	L->author_line = true;
	L->author_color = m->color;
	L->minutes = m->minutes;
	L->fg = COL_TEXT;

	// body, word-wrapped
	const char *p = m->text;
	while (*p)
	{
		// skip leading spaces on continuation lines
		while (*p == ' ')
			p++;
		if (!*p)
			break;

		const char *nl = strchr(p, '\n');
		int avail = nl ? (int)(nl - p) : (int)strlen(p);
		int take = avail < width ? avail : width;

		if (take == width && avail > width)
		{
			// back up to the last space so words stay whole
			int bs = take;
			while (bs > 0 && p[bs] != ' ')
				bs--;
			if (bs > width / 2)
				take = bs;
		}

		wrap_push(p, take, COL_TEXT);
		p += take;
		if (nl && p == nl)
			p++; // consume newline
	}
}

static void draw_chat(void)
{
	Channel *ch = &channels[current_chan];

	if (!lines)
		lines = malloc(MAX_LINES * sizeof(WrapLine));
	line_count = 0;

	int width = chat_cols - 2;
	int start = ch->msg_count > MSGS_PER_CHANNEL
					? ch->msg_count - MSGS_PER_CHANNEL
					: 0;
	for (int i = start; i < ch->msg_count; i++)
		wrap_message(&ch->msgs[i % MSGS_PER_CHANNEL], width);

	fill_cells(chat_x, chat_top, chat_cols, chat_rows, COL_CHAT);

	int visible = line_count < chat_rows ? line_count : chat_rows;
	int src = line_count - visible;
	for (int i = 0; i < visible; i++)
	{
		WrapLine *L = &lines[src + i];
		int y = chat_top + i;
		if (L->author_line)
		{
			put_text(L->text, chat_x + 1, y, L->author_color, COL_CHAT);
			char ts[16];
			snprintf(ts, sizeof(ts), " %02u:%02u", L->minutes / 60,
					 L->minutes % 60);
			put_text(ts, chat_x + 1 + (int)strlen(L->text), y, COL_MUTED,
					 COL_CHAT);
		}
		else
		{
			put_text_n(L->text, chat_cols - 2, chat_x + 1, y, L->fg, COL_CHAT);
		}
	}
}

static void draw_input(void)
{
	Channel *ch = &channels[current_chan];
	int y = rows - input_rows;

	fill_cells(chat_x, y, chat_cols, input_rows, COL_CHAT);
	fill_cells(chat_x + 1, y, chat_cols - 2, 1, COL_INPUT);

	if (input_len == 0)
	{
		char hint[64];
		snprintf(hint, sizeof(hint), "Message %s", ch->name);
		put_text(hint, chat_x + 2, y, COL_MUTED, COL_INPUT);
	}
	else
	{
		int shown = input_len < chat_cols - 5 ? input_len
											  : chat_cols - 5;
		put_text_n(input + input_len - shown, shown, chat_x + 2, y, COL_TEXT,
				   COL_INPUT);
		// block cursor
		fill_cells(chat_x + 2 + shown, y, 1, 1, COL_TEXT);
	}

	if (ch->typing)
	{
		char t[64];
		snprintf(t, sizeof(t), "%s is typing...", ch->bot);
		put_text(t, chat_x + 2, y + 1, COL_MUTED, COL_CHAT);
	}
}

static void draw_all(void)
{
	draw_serverbar();
	draw_sidebar();
	draw_header();
	draw_members();
	draw_chat();
	draw_input();
}

// ---------- messaging ----------

static Message *push_message(Channel *ch, const char *author, uint32_t color)
{
	Message *m = &ch->msgs[ch->msg_count % MSGS_PER_CHANNEL];
	strlcpy(m->author, author, sizeof(m->author));
	m->color = color;
	m->text[0] = '\0';
	m->minutes = (uint32_t)(timer_get_ticks() / 60000);
	ch->msg_count++;
	return m;
}

typedef struct
{
	Channel *chan;
	Message *msg;
	char prompt[INPUT_MAX];
} BotJob;

static BotJob job; // one generation at a time (llm_busy guards anyway)

static void bot_emit(const char *piece, void *ud)
{
	BotJob *j = ud;
	size_t len = strlen(j->msg->text);
	size_t add = strlen(piece);
	if (len + add < MSG_TEXT_MAX - 1)
	{
		memcpy(j->msg->text + len, piece, add + 1);
		j->chan->dirty = true;
	}
}

static void bot_worker(void *arg)
{
	BotJob *j = arg;
	Channel *ch = j->chan;

	int n = llm_generate(ch->model, j->prompt, ch->max_tokens, ch->temp_centi,
						 ch->topp_centi,
						 timer_get_ticks() ^ 0xC0FFEE5EEDull, bot_emit, j);
	if (n < 0)
		strlcpy(j->msg->text,
				n == LLM_ERR_BUSY
					? "(one story at a time! someone else is generating)"
					: "(model unavailable - was the ISO built with models?)",
				MSG_TEXT_MAX);
	if (j->msg->text[0] == '\0')
		strlcpy(j->msg->text, "(the model had nothing to say)", MSG_TEXT_MAX);

	ch->typing = false;
	ch->dirty = true;
}

static void send_message(void)
{
	Channel *ch = &channels[current_chan];
	if (input_len == 0 || ch->typing)
		return;

	input[input_len] = '\0';
	Message *um = push_message(ch, "you", COL_BLURPLE);
	strlcpy(um->text, input, MSG_TEXT_MAX);

	Message *bm = push_message(ch, ch->bot, ch->bot_color);
	job.chan = ch;
	job.msg = bm;
	strlcpy(job.prompt, input, sizeof(job.prompt));

	ch->typing = true;
	ch->dirty = true;
	input_len = 0;

	thread_create("llm-bot", bot_worker, &job);
}

// ---------- main loop ----------

void chat_run(void)
{
	fw = framebuffer_get_font_width();
	fh = framebuffer_get_font_height();
	cols = framebuffer_get_width() / fw;
	rows = framebuffer_get_height() / fh;

	sidebar_cols = 22;
	members_cols = 18;
	chat_x = 3 + sidebar_cols;
	chat_cols = cols - chat_x - members_cols;
	chat_top = header_rows;
	chat_rows = rows - header_rows - input_rows;

	input_len = 0;
	running = true;

	// greet on first open
	if (channels[0].msg_count == 0)
	{
		Message *m = push_message(&channels[0], channels[0].bot,
								  channels[0].bot_color);
		strlcpy(m->text,
				"hey! type the start of a story and i'll finish it. "
				"i'm a 15M-param TinyStories model running inside the kernel, "
				"be gentle",
				MSG_TEXT_MAX);
	}

	framebuffer_clear(COL_CHAT);
	draw_all();

	while (running)
	{
		bool need_input_redraw = false;
		KeyEvent ev;
		while (kbd_poll_event(&ev))
		{
			if (ev.code == KEY_ESCAPE)
			{
				running = false;
				break;
			}
			if (ev.code == KEY_TAB ||
				(ev.code == KEY_CHAR && ev.ch == '\t'))
			{
				current_chan = (current_chan + 1) % NUM_CHANNELS;
				draw_all();
				continue;
			}
			if (ev.code != KEY_CHAR)
				continue;

			if (ev.ch == '\n')
			{
				send_message();
				draw_chat();
				need_input_redraw = true;
			}
			else if (ev.ch == '\b')
			{
				if (input_len > 0)
				{
					input_len--;
					need_input_redraw = true;
				}
			}
			else if (ev.ch >= 32 && ev.ch < 127 &&
					 input_len < INPUT_MAX - 1)
			{
				input[input_len++] = ev.ch;
				need_input_redraw = true;
			}
		}

		Channel *ch = &channels[current_chan];
		if (ch->dirty)
		{
			ch->dirty = false;
			draw_chat();
			need_input_redraw = true; // typing indicator may have changed
		}
		if (need_input_redraw)
			draw_input();

		thread_sleep_ms(16);
	}
}
