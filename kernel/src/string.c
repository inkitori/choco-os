#include "string.h"
#include "malloc.h"

size_t strlen(const char *s)
{
	size_t n = 0;
	while (s[n])
		n++;
	return n;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
	{
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	while (n && *a && *a == *b)
	{
		a++;
		b++;
		n--;
	}
	if (n == 0)
		return 0;
	return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dst, const char *src)
{
	char *d = dst;
	while ((*d++ = *src++))
		;
	return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
	size_t i = 0;
	for (; i < n && src[i]; i++)
		dst[i] = src[i];
	for (; i < n; i++)
		dst[i] = '\0';
	return dst;
}

size_t strlcpy(char *dst, const char *src, size_t size)
{
	size_t len = strlen(src);
	if (size)
	{
		size_t n = len < size - 1 ? len : size - 1;
		memcpy(dst, src, n);
		dst[n] = '\0';
	}
	return len;
}

char *strcat(char *dst, const char *src)
{
	char *d = dst + strlen(dst);
	while ((*d++ = *src++))
		;
	return dst;
}

char *strchr(const char *s, int c)
{
	for (;; s++)
	{
		if (*s == (char)c)
			return (char *)s;
		if (!*s)
			return NULL;
	}
}

char *strrchr(const char *s, int c)
{
	const char *last = NULL;
	for (;; s++)
	{
		if (*s == (char)c)
			last = s;
		if (!*s)
			return (char *)last;
	}
}

char *strstr(const char *haystack, const char *needle)
{
	size_t nlen = strlen(needle);
	if (nlen == 0)
		return (char *)haystack;
	for (; *haystack; haystack++)
	{
		if (strncmp(haystack, needle, nlen) == 0)
			return (char *)haystack;
	}
	return NULL;
}

char *strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = malloc(n);
	if (p)
		memcpy(p, s, n);
	return p;
}

long strtol(const char *s, char **end, int base)
{
	while (*s == ' ' || *s == '\t')
		s++;

	bool neg = false;
	if (*s == '-')
	{
		neg = true;
		s++;
	}
	else if (*s == '+')
	{
		s++;
	}

	if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
	{
		base = 16;
		s += 2;
	}
	else if (base == 0)
	{
		base = (s[0] == '0') ? 8 : 10;
	}

	long val = 0;
	for (;; s++)
	{
		int d;
		if (*s >= '0' && *s <= '9')
			d = *s - '0';
		else if (*s >= 'a' && *s <= 'z')
			d = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'Z')
			d = *s - 'A' + 10;
		else
			break;
		if (d >= base)
			break;
		val = val * base + d;
	}

	if (end)
		*end = (char *)s;
	return neg ? -val : val;
}

int atoi(const char *s)
{
	return (int)strtol(s, NULL, 10);
}
