#ifndef E1000_H
#define E1000_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Intel 82540EM (QEMU's default e1000), polled mode - no interrupts.
bool e1000_init(void);
void e1000_get_mac(uint8_t mac[6]);
int e1000_send(const void *frame, size_t len);
// Receive next pending frame into buf (cap bytes); returns length or 0.
int e1000_recv(void *buf, size_t cap);

#endif
