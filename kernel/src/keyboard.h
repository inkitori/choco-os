#ifndef KEYBOARD_H
#define KEYBOARD_H

#define KEYBOARD_ENABLE_SCANNING 0xF4
#define KEYBOARD_SCAN_CODE_SET 0xF0

#define KEYBOARD_SET_SCAN_CODE_1 0x01
#define KEYBOARD_SET_SCAN_CODE_2 0x02
#define KEYBOARD_SET_SCAN_CODE_3 0x03

void keyboard_init();
void keyboard_handler();
char keyboard_getchar();

#endif