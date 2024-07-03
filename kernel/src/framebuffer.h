#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <limine.h>

void framebuffer_init();
void framebuffer_clear(uint32_t color);

#endif