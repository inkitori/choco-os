#include "kprintf.h"
#include "serial.h"
#include "term.h"
#include "string.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
	char *buf;
	size_t size;
	size_t pos;
} OutBuf;

static void out_char(OutBuf *o, char c)
{
	if (o->pos + 1 < o->size)
		o->buf[o->pos] = c;
	o->pos++;
}

static void out_pad(OutBuf *o, char pad, int count)
{
	while (count-- > 0)
		out_char(o, pad);
}

static const char digits_lower[] = "0123456789abcdef";
static const char digits_upper[] = "0123456789ABCDEF";

static void out_number(OutBuf *o, uint64_t value, int base, bool negative,
					   bool upper, int width, bool zero_pad, bool left)
{
	char tmp[32];
	const char *digits = upper ? digits_upper : digits_lower;
	int len = 0;

	if (value == 0)
		tmp[len++] = '0';
	while (value)
	{
		tmp[len++] = digits[value % base];
		value /= base;
	}
	if (negative)
		tmp[len++] = '-';

	int pad = width - len;
	if (!left)
		out_pad(o, zero_pad ? '0' : ' ', pad);
	while (len)
		out_char(o, tmp[--len]);
	if (left)
		out_pad(o, ' ', pad);
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	OutBuf o = {buf, size, 0};

	for (; *fmt; fmt++)
	{
		if (*fmt != '%')
		{
			out_char(&o, *fmt);
			continue;
		}
		fmt++;

		bool left = false, zero_pad = false;
		while (*fmt == '-' || *fmt == '0')
		{
			if (*fmt == '-')
				left = true;
			else
				zero_pad = true;
			fmt++;
		}

		int width = 0;
		while (*fmt >= '0' && *fmt <= '9')
		{
			width = width * 10 + (*fmt - '0');
			fmt++;
		}

		int longs = 0;
		while (*fmt == 'l')
		{
			longs++;
			fmt++;
		}
		if (*fmt == 'z')
		{
			longs = 2;
			fmt++;
		}

		switch (*fmt)
		{
		case 's':
		{
			const char *s = va_arg(ap, const char *);
			if (!s)
				s = "(null)";
			int len = (int)strlen(s);
			if (!left)
				out_pad(&o, ' ', width - len);
			while (*s)
				out_char(&o, *s++);
			if (left)
				out_pad(&o, ' ', width - len);
			break;
		}
		case 'c':
			out_char(&o, (char)va_arg(ap, int));
			break;
		case 'd':
		case 'i':
		{
			int64_t v = longs ? va_arg(ap, int64_t) : va_arg(ap, int32_t);
			bool neg = v < 0;
			out_number(&o, neg ? (uint64_t)(-v) : (uint64_t)v, 10, neg, false,
					   width, zero_pad, left);
			break;
		}
		case 'u':
		{
			uint64_t v = longs ? va_arg(ap, uint64_t) : va_arg(ap, uint32_t);
			out_number(&o, v, 10, false, false, width, zero_pad, left);
			break;
		}
		case 'x':
		case 'X':
		{
			uint64_t v = longs ? va_arg(ap, uint64_t) : va_arg(ap, uint32_t);
			out_number(&o, v, 16, false, *fmt == 'X', width, zero_pad, left);
			break;
		}
		case 'p':
		{
			out_char(&o, '0');
			out_char(&o, 'x');
			out_number(&o, (uint64_t)va_arg(ap, void *), 16, false, false, 16,
					   true, false);
			break;
		}
		case '%':
			out_char(&o, '%');
			break;
		default:
			out_char(&o, '%');
			if (*fmt)
				out_char(&o, *fmt);
			else
				fmt--;
			break;
		}
	}

	if (size)
		buf[o.pos < size - 1 ? o.pos : size - 1] = '\0';
	return (int)o.pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return r;
}

void kprintf(const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	serial_write(buf);
}

void term_printf(const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	term_print(buf); // term mirrors to serial already
}
