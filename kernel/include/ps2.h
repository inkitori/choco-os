#ifndef PS2_H
#define PS2_H
#include <stdint.h>

#define DATA_PORT 0x60
#define STATUS_REGISTER 0x64
#define COMMAND_PORT 0x64

#define DEVICE_COMMAND_RESET 0xFF

#define DEVICE_ACK 0xFA
#define DEVICE_RESEND 0xFE
#define DEVICE_SELF_TEST_PASSED 0xAA
#define DEVICE_SELF_TEST_FAILED_1 0xFC
#define DEVICE_SELF_TEST_FAILED_2 0xFD

#define STATUS_OUTPUT_BUFFER 0
#define STATUS_INPUT_BUFFER 1

#define STATUS_OUTPUT_BUFFER_MASK (1 << STATUS_OUTPUT_BUFFER)
#define STATUS_INPUT_BUFFER_MASK (1 << STATUS_INPUT_BUFFER)

#define COMMAND_READ_CONFIG_BYTE 0x20
#define COMMAND_WRITE_CONFIG_BYTE 0x60
#define COMMAND_DISABLE_FIRST_PORT 0xAD
#define COMMAND_ENABLE_FIRST_PORT 0xAE
#define COMMAND_DISABLE_SECOND_PORT 0xA7
#define COMMAND_ENABLE_SECOND_PORT 0xA8
#define COMMAND_TEST_CONTROLLER 0xAA
#define COMMAND_TEST_FIRST_PORT 0xAB
#define COMMAND_TEST_SECOND_PORT 0xA9
#define COMMAND_WRITE_SECOND_PORT 0xD4

#define RESPONSE_CONTROLLER_TEST_PASSED 0x55
#define RESPONSE_CONTROLLER_TEST_FAILED 0xFC
#define RESPONSE_PORT_TEST_PASSED 0x00
#define RESPONSE_PORT_TEST_CLOCK_LOW 0x01
#define RESPONSE_PORT_TEST_CLOCK_HIGH 0x02
#define RESPONSE_PORT_TEST_DATA_LOW 0x03
#define RESPONSE_PORT_TEST_DATA_HIGH 0x04

#define CONFIG_FIRST_PORT_INTERRUPT 0
#define CONFIG_SECOND_PORT_INTERRUPT 1
#define CONFIG_FIRST_PORT_CLOCK 4
#define CONFIG_SECOND_PORT_CLOCK 5
#define CONFIG_FIRST_PORT_TRANSLATION 6

#define CONFIG_FIRST_PORT_INTERRUPT_MASK (1 << CONFIG_FIRST_PORT_INTERRUPT)
#define CONFIG_SECOND_PORT_INTERRUPT_MASK (1 << CONFIG_SECOND_PORT_INTERRUPT)
#define CONFIG_FIRST_PORT_CLOCK_MASK (1 << CONFIG_FIRST_PORT_CLOCK)
#define CONFIG_SECOND_PORT_CLOCK_MASK (1 << CONFIG_SECOND_PORT_CLOCK)
#define CONFIG_FIRST_PORT_TRANSLATION_MASK (1 << CONFIG_FIRST_PORT_TRANSLATION)

void ps2_init_controller();

void ps2_data_out(uint8_t data);
uint8_t ps2_data_in();

#endif