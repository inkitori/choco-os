#include "lib.h"

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