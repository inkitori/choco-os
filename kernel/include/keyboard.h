#ifndef KEYBOARD_H
#define KEYBOARD_H
#include "stdint.h"

#define KEYBOARD_ENABLE_SCANNING 0xF4
#define KEYBOARD_SCAN_CODE_SET 0xF0

#define KEYBOARD_SET_SCAN_CODE_1 0x01
#define KEYBOARD_SET_SCAN_CODE_2 0x02
#define KEYBOARD_SET_SCAN_CODE_3 0x03

#define KEYBOARD_SCAN_CODE_A 0x1C
#define KEYBOARD_SCAN_CODE_S 0x1B
#define KEYBOARD_SCAN_CODE_D 0x23
#define KEYBOARD_SCAN_CODE_W 0x1D

void keyboard_init();
void keyboard_handler();
uint8_t keyboard_get_scan_code();

#endif