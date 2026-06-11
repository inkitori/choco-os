// Interactive shell: line editing, command history, argument parsing.
#include "shell.h"
#include "term.h"
#include "keyboard.h"
#include "string.h"
#include "kprintf.h"
#include "snake.h"
#include "pong.h"
#include "mem.h"
#include "malloc.h"
#include "pmm.h"
#include "vmm.h"
#include "timer.h"
#include "sched.h"
#include "vfs.h"
#include "io.h"
#include "lib.h"
#include "llm.h"
#include "serial.h"
#include "chat.h"
#include "rtc.h"
#include "net.h"

#include <stdint.h>
#include <stdbool.h>

#define LINE_MAX 256
#define HISTORY_MAX 16
#define ARGV_MAX 16

static char history[HISTORY_MAX][LINE_MAX];
static int history_count = 0;

static VfsNode *cwd = NULL;

static void prompt(void)
{
	char path[128];
	vfs_node_path(cwd ? cwd : vfs_root(), path, sizeof(path));
	term_print_with_color("choco", TERM_COLOR_MAGENTA, TERM_COLOR_BLACK);
	term_print_with_color(" ", TERM_COLOR_WHITE, TERM_COLOR_BLACK);
	term_print_with_color(path, TERM_COLOR_CYAN, TERM_COLOR_BLACK);
	term_print_with_color(" > ", TERM_COLOR_WHITE, TERM_COLOR_BLACK);
}

// ---- commands ----

typedef struct
{
	const char *name;
	const char *help;
	void (*fn)(int argc, char **argv);
} Command;

static void cmd_help(int argc, char **argv);

static void cmd_fetch(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	uint32_t brown = 0x8b4513;
	uint32_t key_color = 0xe80c5c;

	uint64_t up = timer_get_ticks() / 1000;
	char upbuf[64];
	snprintf(upbuf, sizeof(upbuf), "%lum %lus\n", up / 60, up % 60);
	char membuf[64];
	snprintf(membuf, sizeof(membuf), "%lu/%lu MiB heap\n",
			 (uint64_t)(heap_used() / (1024 * 1024)),
			 (uint64_t)(heap_total() / (1024 * 1024)));

	term_print("\n");
	term_print_with_color("    /\\_/\\       ", brown, TERM_COLOR_BLACK);
	term_print_with_color("OS: ", key_color, TERM_COLOR_BLACK);
	term_print("Choco OS x86_64\n");

	term_print_with_color("   ( o.o )      ", brown, TERM_COLOR_BLACK);
	term_print_with_color("Kernel: ", key_color, TERM_COLOR_BLACK);
	term_print("Choco Kernel (threads + ramfs + llm)\n");

	term_print_with_color("    > ^ <       ", brown, TERM_COLOR_BLACK);
	term_print_with_color("Uptime: ", key_color, TERM_COLOR_BLACK);
	term_print(upbuf);

	term_print_with_color("                ", brown, TERM_COLOR_BLACK);
	term_print_with_color("Memory: ", key_color, TERM_COLOR_BLACK);
	term_print(membuf);
	term_print("\n");
}

static void cmd_clear(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	term_clear(TERM_COLOR_BLACK);
}

static void cmd_echo(int argc, char **argv)
{
	for (int i = 1; i < argc; i++)
	{
		term_print(argv[i]);
		if (i + 1 < argc)
			term_print(" ");
	}
	term_print("\n");
}

static void cmd_uptime(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	uint64_t ms = timer_get_ticks();
	term_printf("up %lu:%02lu:%02lu (%lu ms)\n", ms / 3600000,
				(ms / 60000) % 60, (ms / 1000) % 60, ms);
}

static void cmd_free(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	term_printf("heap: %lu KiB used / %lu KiB total\n",
				(uint64_t)(heap_used() / 1024),
				(uint64_t)(heap_total() / 1024));
}

static void cmd_snake(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	term_set_cursor_visible(false);
	snake_init();
	term_set_cursor_visible(true);
	term_clear(TERM_COLOR_BLACK);
}

static void cmd_pong(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	term_set_cursor_visible(false);
	pong_init();
	term_set_cursor_visible(true);
	term_clear(TERM_COLOR_BLACK);
}

static void cmd_memmap(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	debug_memmap();
}

static void cmd_testmalloc(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	test_malloc();
}

static void cmd_ps(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	sched_dump();
}

static void cmd_ls(int argc, char **argv)
{
	VfsNode *dir = cwd;
	if (argc > 1)
	{
		dir = vfs_resolve(argv[1], cwd);
		if (!dir)
		{
			term_printf("ls: %s: not found\n", argv[1]);
			return;
		}
	}
	if (!dir->is_dir)
	{
		term_printf("%s\n", dir->name);
		return;
	}
	for (VfsNode *c = dir->children; c; c = c->next)
	{
		if (c->is_dir)
			term_print_with_color(c->name, TERM_COLOR_CYAN, TERM_COLOR_BLACK);
		else
			term_print(c->name);
		if (!c->is_dir)
			term_printf("  (%lu)", (uint64_t)c->size);
		term_print("\n");
	}
}

static void cmd_cat(int argc, char **argv)
{
	if (argc < 2)
	{
		term_print("usage: cat <file>\n");
		return;
	}
	VfsNode *f = vfs_resolve(argv[1], cwd);
	if (!f || f->is_dir)
	{
		term_printf("cat: %s: not found\n", argv[1]);
		return;
	}
	for (size_t i = 0; i < f->size; i++)
	{
		char c = (char)f->data[i];
		if (c == '\t' || c == '\n' || (c >= 32 && c < 127))
			term_print_char(c, term_get_fg(), term_get_bg());
		else
			term_print_char('.', TERM_COLOR_GRAY, term_get_bg());
	}
	if (f->size && f->data[f->size - 1] != '\n')
		term_print("\n");
}

static void cmd_write(int argc, char **argv)
{
	if (argc < 3)
	{
		term_print("usage: write <file> <text...>\n");
		return;
	}
	const char *leaf = NULL;
	VfsNode *dir = vfs_resolve_parent(argv[1], cwd, &leaf);
	if (!dir || !leaf)
	{
		term_printf("write: bad path %s\n", argv[1]);
		return;
	}
	char name[VFS_NAME_MAX];
	strlcpy(name, leaf, sizeof(name));
	char *slash = strchr(name, '/');
	if (slash)
		*slash = '\0';

	VfsNode *f = vfs_resolve(argv[1], cwd);
	if (!f)
		f = vfs_create(dir, name, false);
	if (!f || f->is_dir || f->readonly)
	{
		term_printf("write: cannot write %s\n", argv[1]);
		return;
	}

	vfs_truncate(f, 0);
	size_t off = 0;
	for (int i = 2; i < argc; i++)
	{
		if (i > 2)
			vfs_write(f, " ", off++, 1);
		vfs_write(f, argv[i], off, strlen(argv[i]));
		off += strlen(argv[i]);
	}
	vfs_write(f, "\n", off, 1);
}

static void cmd_mkdir(int argc, char **argv)
{
	if (argc < 2)
	{
		term_print("usage: mkdir <dir>\n");
		return;
	}
	if (!vfs_mkdirs(argv[1], cwd))
		term_printf("mkdir: cannot create %s\n", argv[1]);
}

static void cmd_rm(int argc, char **argv)
{
	if (argc < 2)
	{
		term_print("usage: rm <path>\n");
		return;
	}
	VfsNode *n = vfs_resolve(argv[1], cwd);
	if (!n)
	{
		term_printf("rm: %s: not found\n", argv[1]);
		return;
	}
	if (vfs_unlink(n) < 0)
		term_printf("rm: cannot remove %s\n", argv[1]);
}

static void cmd_cd(int argc, char **argv)
{
	if (argc < 2)
	{
		cwd = vfs_root();
		return;
	}
	VfsNode *n = vfs_resolve(argv[1], cwd);
	if (!n || !n->is_dir)
	{
		term_printf("cd: %s: not a directory\n", argv[1]);
		return;
	}
	cwd = n;
}

static void cmd_pwd(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	char path[128];
	vfs_node_path(cwd, path, sizeof(path));
	term_printf("%s\n", path);
}

static void cmd_reboot(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	outb(0x64, 0xFE); // 8042 CPU reset pulse
}

static void cmd_shutdown(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	outw(0x604, 0x2000); // QEMU q35 ACPI S5
	term_print("shutdown failed (not running under QEMU?)\n");
}

static void cmd_sleep(int argc, char **argv)
{
	if (argc < 2)
	{
		term_print("usage: sleep <ms>\n");
		return;
	}
	sleep_ms((uint64_t)atoi(argv[1]));
}

static bool net_ensure(void)
{
	if (!net_up())
	{
		term_print("no network card (run QEMU with -device e1000)\n");
		return false;
	}
	if (!net_configured())
	{
		term_print("acquiring address via DHCP...\n");
		if (!net_dhcp(5000))
		{
			term_print_error("DHCP failed\n");
			return false;
		}
	}
	return true;
}

static void cmd_ifconfig(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (!net_ensure())
		return;

	uint8_t mac[6];
	net_mac(mac);
	char ip[16], mask[16], gw[16], dns[16];
	ip_to_str(net_ip(), ip);
	ip_to_str(net_mask(), mask);
	ip_to_str(net_gateway(), gw);
	ip_to_str(net_dns_server(), dns);

	term_printf("eth0: %s\n", ip);
	term_printf("  mask %s  gateway %s  dns %s\n", mask, gw, dns);
	term_printf("  mac %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1],
				mac[2], mac[3], mac[4], mac[5]);
}

static void cmd_ping(int argc, char **argv)
{
	if (argc < 2)
	{
		term_print("usage: ping <host> [count]\n");
		return;
	}
	if (!net_ensure())
		return;

	uint32_t ip;
	if (!net_resolve(argv[1], &ip, 3000))
	{
		term_printf("ping: cannot resolve %s\n", argv[1]);
		return;
	}

	char ipstr[16];
	ip_to_str(ip, ipstr);
	int count = argc > 2 ? atoi(argv[2]) : 4;
	if (count < 1 || count > 100)
		count = 4;

	int received = 0;
	for (int i = 0; i < count; i++)
	{
		int rtt = net_ping(ip, (uint16_t)(i + 1), 2000);
		if (rtt >= 0)
		{
			term_printf("64 bytes from %s: icmp_seq=%d time=%d ms\n", ipstr,
						i + 1, rtt);
			received++;
		}
		else
		{
			term_printf("icmp_seq=%d timeout\n", i + 1);
		}
		if (i + 1 < count)
			sleep_ms(500);
	}
	term_printf("--- %s: %d/%d received ---\n", ipstr, received, count);
}

static void cmd_nslookup(int argc, char **argv)
{
	if (argc < 2)
	{
		term_print("usage: nslookup <name>\n");
		return;
	}
	if (!net_ensure())
		return;

	uint32_t ip;
	if (!net_resolve(argv[1], &ip, 3000))
	{
		term_printf("nslookup: cannot resolve %s\n", argv[1]);
		return;
	}
	char ipstr[16];
	ip_to_str(ip, ipstr);
	term_printf("%s -> %s\n", argv[1], ipstr);
}

static void cmd_date(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
								   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	RtcTime t = rtc_read();
	const char *mon = (t.month >= 1 && t.month <= 12) ? months[t.month - 1]
													  : "???";
	term_printf("%s %d %d %02d:%02d:%02d UTC\n", mon, t.day, t.year, t.hour,
				t.minute, t.second);
}

static void cmd_history(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	for (int i = 0; i < history_count; i++)
		term_printf("  %d  %s\n", i + 1, history[i]);
}

static void cmd_chat(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	term_set_cursor_visible(false);
	kbd_flush();
	chat_run();
	kbd_flush();
	term_set_cursor_visible(true);
	term_clear(TERM_COLOR_BLACK);
}

static void llm_emit_term(const char *piece, void *ud)
{
	(void)ud;
	term_print(piece);
	serial_write(piece);
}

static void cmd_llm(int argc, char **argv)
{
	const char *model = "stories15M";
	int max_tokens = 0;
	int temp_centi = 80;
	int topp_centi = 90;

	char prompt[LINE_MAX];
	prompt[0] = '\0';
	size_t plen = 0;

	int i = 1;
	for (; i < argc; i++)
	{
		if (strcmp(argv[i], "-m") == 0 && i + 1 < argc)
			model = argv[++i];
		else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
			max_tokens = atoi(argv[++i]);
		else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
			temp_centi = atoi(argv[++i]);
		else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
			topp_centi = atoi(argv[++i]);
		else if (strcmp(argv[i], "-i") == 0)
		{
			const char *info = llm_describe(model);
			term_printf("%s: %s\n", model, info ? info : "unavailable");
			return;
		}
		else
			break;
	}

	for (; i < argc; i++)
	{
		if (plen && plen + 1 < sizeof(prompt))
			prompt[plen++] = ' ';
		size_t alen = strlen(argv[i]);
		if (plen + alen < sizeof(prompt))
		{
			memcpy(prompt + plen, argv[i], alen);
			plen += alen;
		}
	}
	prompt[plen] = '\0';

	if (plen == 0)
	{
		term_print("usage: llm [-m model] [-n tokens] [-t temp%] [-p topp%] <prompt>\n");
		term_print("       llm -i [-m model]    show model info\n");
		term_print("models: stories15M (default), stories260K (fast)\n");
		return;
	}

	uint64_t t0 = timer_get_ticks();
	term_print_with_color(prompt, TERM_COLOR_YELLOW, TERM_COLOR_BLACK);
	int n = llm_generate(model, prompt, max_tokens, temp_centi, topp_centi,
						 timer_get_ticks() ^ 0x9E3779B97F4A7C15ull, 0,
						 llm_emit_term, NULL);
	uint64_t dt = timer_get_ticks() - t0;

	if (n < 0)
	{
		if (n == LLM_ERR_NO_MODEL)
			term_printf("\nllm: model '%s' not found (boot with the model module)\n", model);
		else if (n == LLM_ERR_BUSY)
			term_print("\nllm: another generation is in progress\n");
		else
			term_printf("\nllm: error %d\n", n);
		return;
	}

	uint64_t toks_per_10s = dt ? (uint64_t)n * 10000 / dt : 0;
	term_printf("\n");
	term_printf("[%d tokens in %lu.%lus, %lu.%lu tok/s]\n", n, dt / 1000,
				(dt % 1000) / 100, toks_per_10s / 10, toks_per_10s % 10);
}

static const Command commands[] = {
	{"help", "list commands", cmd_help},
	{"fetch", "system info", cmd_fetch},
	{"clear", "clear the screen", cmd_clear},
	{"echo", "print arguments", cmd_echo},
	{"uptime", "time since boot", cmd_uptime},
	{"free", "heap usage", cmd_free},
	{"ls", "list directory", cmd_ls},
	{"cat", "print file contents", cmd_cat},
	{"write", "write text to a file", cmd_write},
	{"mkdir", "create directory", cmd_mkdir},
	{"rm", "remove file or empty dir", cmd_rm},
	{"cd", "change directory", cmd_cd},
	{"pwd", "print working directory", cmd_pwd},
	{"ps", "list threads", cmd_ps},
	{"ifconfig", "network status (runs DHCP)", cmd_ifconfig},
	{"ping", "ICMP echo a host", cmd_ping},
	{"nslookup", "resolve a hostname", cmd_nslookup},
	{"date", "read the real-time clock", cmd_date},
	{"history", "show command history", cmd_history},
	{"sleep", "sleep N milliseconds", cmd_sleep},
	{"llm", "generate text with the in-kernel LLM", cmd_llm},
	{"chat", "ChocoCord: chat with the LLM bots", cmd_chat},
	{"snake", "play snake", cmd_snake},
	{"pong", "play pong", cmd_pong},
	{"memmap", "physical memory map", cmd_memmap},
	{"testmalloc", "heap self-test", cmd_testmalloc},
	{"reboot", "reboot the machine", cmd_reboot},
	{"shutdown", "power off", cmd_shutdown},
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

static void cmd_help(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	for (unsigned i = 0; i < COMMAND_COUNT; i++)
	{
		char buf[80];
		snprintf(buf, sizeof(buf), "  %-12s %s\n", commands[i].name,
				 commands[i].help);
		term_print(buf);
	}
}

static void process_command(char *line)
{
	char *argv[ARGV_MAX];
	int argc = 0;

	char *p = line;
	while (*p && argc < ARGV_MAX)
	{
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;
		argv[argc++] = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		if (*p)
			*p++ = '\0';
	}
	if (argc == 0)
		return;

	for (unsigned i = 0; i < COMMAND_COUNT; i++)
	{
		if (strcmp(argv[0], commands[i].name) == 0)
		{
			commands[i].fn(argc, argv);
			return;
		}
	}
	term_printf("%s: command not found (try 'help')\n", argv[0]);
}

static void redraw_line(const char *buf, int old_len)
{
	for (int i = 0; i < old_len; i++)
		term_print("\b");
	term_print(buf);
	int new_len = (int)strlen(buf);
	for (int i = new_len; i < old_len; i++)
		term_print(" ");
	for (int i = new_len; i < old_len; i++)
		term_print("\b");
}

void shell_init(void)
{
	char buf[LINE_MAX];
	int len = 0;
	int hist_pos = -1;

	cwd = vfs_root();

	term_clear(TERM_COLOR_BLACK);
	term_print_with_color(
		"\n"
		"   ###  #   #  ###   ###  ###     ###   ###\n"
		"  #     #   # #   # #    #   #   #   # #\n"
		"  #     ##### #   # #    #   #   #   #  ###\n"
		"  #     #   # #   # #    #   #   #   #     #\n"
		"   ###  #   #  ###   ###  ###     ###   ###\n",
		0xC08552, TERM_COLOR_BLACK);
	term_print_with_color(
		"        threads * ramfs * in-kernel llm\n\n",
		TERM_COLOR_GRAY, TERM_COLOR_BLACK);

	VfsNode *motd = vfs_resolve("/etc/motd", NULL);
	if (motd && !motd->is_dir)
	{
		for (size_t i = 0; i < motd->size; i++)
			term_print_char((char)motd->data[i], TERM_COLOR_WHITE,
							TERM_COLOR_BLACK);
		term_print("\n");
	}

	prompt();

	for (;;)
	{
		KeyEvent ev = kbd_wait_event();

		if (ev.code == KEY_UP || ev.code == KEY_DOWN)
		{
			if (history_count == 0)
				continue;
			if (ev.code == KEY_UP && hist_pos + 1 < history_count)
				hist_pos++;
			else if (ev.code == KEY_DOWN && hist_pos >= 0)
				hist_pos--;
			int old_len = len;
			if (hist_pos < 0)
				buf[0] = '\0';
			else
				strlcpy(buf, history[history_count - 1 - hist_pos], LINE_MAX);
			len = (int)strlen(buf);
			redraw_line(buf, old_len);
			continue;
		}

		if (ev.code != KEY_CHAR)
			continue;

		if (ev.ctrl && (ev.ch == 'l' || ev.ch == 'L'))
		{
			term_clear(TERM_COLOR_BLACK);
			buf[len] = '\0';
			prompt();
			term_print(buf);
			continue;
		}
		if (ev.ctrl && (ev.ch == 'c' || ev.ch == 'C'))
		{
			term_print("^C\n");
			len = 0;
			hist_pos = -1;
			prompt();
			continue;
		}
		if (ev.ctrl)
			continue;

		if (ev.ch == '\n')
		{
			term_print("\n");
			buf[len] = '\0';

			if (len > 0)
			{
				if (history_count == 0 ||
					strcmp(history[history_count - 1], buf) != 0)
				{
					if (history_count == HISTORY_MAX)
					{
						for (int i = 1; i < HISTORY_MAX; i++)
							strcpy(history[i - 1], history[i]);
						history_count--;
					}
					strcpy(history[history_count++], buf);
				}
				process_command(buf);
			}

			len = 0;
			hist_pos = -1;
			prompt();
			continue;
		}

		if (ev.ch == '\b')
		{
			if (len > 0)
			{
				len--;
				term_print_char('\b', term_get_fg(), term_get_bg());
			}
			continue;
		}

		if (len < LINE_MAX - 1 && ev.ch >= 32 && ev.ch < 127)
		{
			buf[len++] = ev.ch;
			term_print_char(ev.ch, term_get_fg(), term_get_bg());
		}
	}
}
