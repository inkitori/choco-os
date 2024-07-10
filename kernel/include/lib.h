#ifndef LIB_H
#define LIB_H

#include "stdint.h"
#include "stddef.h"
#include "stdbool.h"

void to_string(uint64_t num, char *str);
bool cmp_string(char *a, char *b);

#endif