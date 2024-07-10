#include "lib.h"
#include "term.h"
#include "stdbool.h"

void to_string(uint64_t num, char *str)
{
	int i = 0;
	while (num > 0)
	{
		str[i] = num % 10 + '0';
		num /= 10;
		i++;
	}
	str[i] = '\0';
	int j = 0;
	i--;
	while (j < i)
	{
		char temp = str[j];
		str[j] = str[i];
		str[i] = temp;
		j++;
		i--;
	}
}

bool cmp_string(char *a, char *b)
{
	uint8_t index = 0;
	while (a[index] != '\0' || b[index] != '\0')
	{
		if (a[index] != b[index])
			return false;
		index++;
	}

	return true;
}