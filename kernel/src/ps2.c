#include "term.h"
#include "io.h"
#include "ps2.h"
#include "stdbool.h"
#include "asm_wrappers.h"
#include "lib.h"

static bool dual_channel = false;

static inline void ps2_wait_for_output_buf()
{
	while (!(inb(STATUS_REGISTER) & STATUS_OUTPUT_BUFFER_MASK))
		;
}

static inline void ps2_wait_for_input_buf()
{
	while (inb(STATUS_REGISTER) & STATUS_INPUT_BUFFER_MASK)
		;
}

void ps2_data_out(uint8_t data)
{
	ps2_wait_for_input_buf();

	outb(DATA_PORT, data);
}

uint8_t ps2_data_in()
{
	ps2_wait_for_output_buf();

	return inb(DATA_PORT);
}

void print_config_byte()
{
	char config_byte_buf[64];

	outb(COMMAND_PORT, COMMAND_READ_CONFIG_BYTE);
	uint8_t config_byte = ps2_data_in();

	to_string(config_byte, config_byte_buf);
	term_print("Config byte: ");
	term_print(config_byte_buf);
}

static void ps2_test_device_reset_response_byte(uint8_t response)
{
	if (response == DEVICE_ACK)
	{
		term_print("PS/2 device reset acknowledged\n");

		uint8_t self_test_response = ps2_data_in();

		switch (self_test_response)
		{

		case DEVICE_SELF_TEST_PASSED:
			term_print_success("PS/2 device self test passed\n");
			break;

		case DEVICE_SELF_TEST_FAILED_1:
			term_print_error("PS/2 device self test failed: First failure code\n");
			hcf();
			break;
		case DEVICE_SELF_TEST_FAILED_2:
			term_print_error("PS/2 device self test failed: Second failure code\n");
			hcf();
			break;
		default:
			term_print_error("PS/2 device self test failed: Unknown response byte\n");
			char buf[64];

			to_string(self_test_response, buf);
			term_print(buf);
			hcf();
		}
	}
	else if (response == DEVICE_RESEND)
	{
		term_print_error("PS/2 device reset request resent");
		hcf();
	}
	else
	{
		term_print_error("PS/2 device reset failed: Unknown response byte");
		char buf[64];

		to_string(response, buf);
		term_print(buf);
		hcf();
	}
}

static void ps2_test_port_response_byte(uint8_t response)
{
	switch (response)
	{
	case RESPONSE_PORT_TEST_PASSED:
		term_print_success("PS/2 port test passed");
		break;
	case RESPONSE_PORT_TEST_CLOCK_LOW:
		term_print_error("PS/2 port test failed: Clock line low");
		hcf();
		break;
	case RESPONSE_PORT_TEST_CLOCK_HIGH:
		term_print_error("PS/2 port test failed: Clock line high");
		hcf();
		break;
	case RESPONSE_PORT_TEST_DATA_LOW:
		term_print_error("PS/2 port test failed: Data line low");
		hcf();
		break;
	case RESPONSE_PORT_TEST_DATA_HIGH:
		term_print_error("PS/2 port test failed: Data line high");
		hcf();
		break;
	default:
		term_print_error("PS/2 port test failed: Unknown response byte");
		hcf();
	}
}

// TODO: split up this function
void ps2_init_controller()
{
	// TODO: Initialise USB controller and disable USB legacy support
	// TODO: Check for existence of PS/2 controller

	// Disabling PS/2 devices
	term_print("Disabling PS/2 devices\n");

	outb(COMMAND_PORT, COMMAND_DISABLE_FIRST_PORT);
	outb(COMMAND_PORT, COMMAND_DISABLE_SECOND_PORT);

	// Flushing the output buffer
	term_print("Flushing PS/2 output buffer\n");

	while (inb(STATUS_REGISTER) & STATUS_OUTPUT_BUFFER_MASK)
		inb(DATA_PORT);

	// Disabling IRQs and translation
	term_print("Disabling PS/2 IRQs and translation\n");

	outb(COMMAND_PORT, COMMAND_READ_CONFIG_BYTE);

	uint8_t config_byte = ps2_data_in();

	config_byte &= ~(CONFIG_FIRST_PORT_INTERRUPT_MASK | CONFIG_SECOND_PORT_INTERRUPT_MASK | CONFIG_FIRST_PORT_TRANSLATION_MASK);
	outb(COMMAND_PORT, COMMAND_WRITE_CONFIG_BYTE);

	ps2_data_out(config_byte);

	// Testing for dual channel
	dual_channel = (config_byte & CONFIG_SECOND_PORT_CLOCK_MASK);
	if (dual_channel)
	{
		term_print_success("PS/2 initially detected as dual channel\n");
	}
	else
	{
		term_print_success("PS/2 confirmed as single channel\n");
	}

	// Performing controller self test
	term_print("Initiating PS/2 controller self test\n");

	outb(COMMAND_PORT, COMMAND_TEST_CONTROLLER);

	uint8_t response = ps2_data_in();

	if (response == RESPONSE_CONTROLLER_TEST_PASSED)
	{
		term_print_success("PS/2 controller self test passed\n");
	}
	else
	{
		term_print_error("PS/2 controller self test failed\n");
		hcf();
	}

	// determining if the controller has a second channel
	if (dual_channel)
	{
		outb(COMMAND_PORT, COMMAND_ENABLE_SECOND_PORT);
		outb(COMMAND_PORT, COMMAND_READ_CONFIG_BYTE);

		config_byte = ps2_data_in();

		dual_channel = !(config_byte & CONFIG_SECOND_PORT_CLOCK_MASK);

		if (dual_channel)
		{
			term_print_success("PS/2 confirmed as dual channel\n");
		}
		else
		{
			term_print_success("PS/2 confirmed as single channel\n");
		}
	}

	// Testing PS/2 ports

	// Testing first port
	outb(COMMAND_PORT, COMMAND_TEST_FIRST_PORT);

	term_print("Testing PS/2 first port\n");
	ps2_test_port_response_byte(ps2_data_in());

	if (dual_channel)
	{
		// Testing second port
		outb(COMMAND_PORT, COMMAND_TEST_SECOND_PORT);

		term_print("Testing PS/2 second port\n");
		ps2_test_port_response_byte(ps2_data_in());
	}
	else
		term_print("Skipping PS/2 second port test\n");

	// Enabling PS/2 devices
	term_print("Enabling PS/2 devices\n");

	outb(COMMAND_PORT, COMMAND_ENABLE_FIRST_PORT);
	if (dual_channel)
		outb(COMMAND_PORT, COMMAND_ENABLE_SECOND_PORT);

	// Enabling PS/2 IRQs
	term_print("Enabling PS/2 IRQs\n");

	outb(COMMAND_PORT, COMMAND_READ_CONFIG_BYTE);

	config_byte = ps2_data_in();
	config_byte |= CONFIG_FIRST_PORT_INTERRUPT_MASK;
	if (dual_channel)
		config_byte |= CONFIG_SECOND_PORT_INTERRUPT_MASK;

	outb(COMMAND_PORT, COMMAND_WRITE_CONFIG_BYTE);

	ps2_data_out(config_byte);

	// Reset devices
	term_print("Resetting PS/2 devices\n");

	term_print("Resetting PS/2 devices on port 1\n");

	ps2_data_out(DEVICE_COMMAND_RESET);

	ps2_test_device_reset_response_byte(ps2_data_in());

	if (dual_channel)
	{
		term_print("Resetting PS/2 devices on port 2\n");

		outb(COMMAND_PORT, COMMAND_WRITE_SECOND_PORT);

		ps2_data_out(DEVICE_COMMAND_RESET);

		ps2_test_device_reset_response_byte(ps2_data_in());

		ps2_data_in(); // tbh im not sure if i need to leave this here, looks like it's just remnants from the mouse handler on port 2 spitting out 0x0 so ill flush it anyways
	}
	else
		term_print("Skipping PS/2 second port reset\n");

	term_print("PS/2 controller initialised\n");
}