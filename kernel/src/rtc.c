// CMOS real-time clock.
#include "rtc.h"
#include "io.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos_read(uint8_t reg)
{
	outb(CMOS_ADDR, reg);
	return inb(CMOS_DATA);
}

static int update_in_progress(void)
{
	return cmos_read(0x0A) & 0x80;
}

RtcTime rtc_read(void)
{
	RtcTime t;
	uint8_t century = 0;

	while (update_in_progress())
		;

	t.second = cmos_read(0x00);
	t.minute = cmos_read(0x02);
	t.hour = cmos_read(0x04);
	t.day = cmos_read(0x07);
	t.month = cmos_read(0x08);
	uint8_t year = cmos_read(0x09);
	century = cmos_read(0x32);

	uint8_t status_b = cmos_read(0x0B);
	if (!(status_b & 0x04)) // BCD mode
	{
		t.second = (t.second & 0x0F) + ((t.second / 16) * 10);
		t.minute = (t.minute & 0x0F) + ((t.minute / 16) * 10);
		t.hour = ((t.hour & 0x0F) + (((t.hour & 0x70) / 16) * 10)) |
				 (t.hour & 0x80);
		t.day = (t.day & 0x0F) + ((t.day / 16) * 10);
		t.month = (t.month & 0x0F) + ((t.month / 16) * 10);
		year = (year & 0x0F) + ((year / 16) * 10);
		century = (century & 0x0F) + ((century / 16) * 10);
	}

	if (!(status_b & 0x02) && (t.hour & 0x80)) // 12h mode, PM flag
		t.hour = ((t.hour & 0x7F) + 12) % 24;

	t.year = century ? century * 100 + year : 2000 + year;
	return t;
}
