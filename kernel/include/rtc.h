#ifndef RTC_H
#define RTC_H

#include <stdint.h>

typedef struct
{
	uint16_t year;
	uint8_t month, day, hour, minute, second;
} RtcTime;

RtcTime rtc_read(void);

#endif
