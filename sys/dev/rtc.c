/*	$e1ruBSD: rtc.c,v 1.1 2026/11/07 00:00:00 e1 Exp $	*/

/*
 * CMOS real-time clock (ports 0x70/0x71).  The RTC keeps whatever time the
 * firmware configured; date(1) reports it as-is — there is no timezone
 * database or NTP client in this system.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syscall.h>
#include <machine/cpu.h>

#define	RTC_ADDR	0x70
#define	RTC_DATA	0x71

static int
rtc_cmos(int reg)
{
	outb(RTC_ADDR, (uint8_t)reg);
	return inb(RTC_DATA);
}

static void
rtc_wait_ready(void)
{
	for (int i = 0; i < 1000000; i++)
		if ((rtc_cmos(0x0A) & 0x80) == 0)
			return;
}

static int
rtc_bcd(int v)
{
	return (v & 0x0F) + ((v >> 4) & 0x0F) * 10;
}

static int
rtc_wday(int y, int m, int d)
{
	static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };

	if (m < 3)
		y -= 1;
	return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

void
rtc_read(struct rtc_time *rt)
{
	int a[7], b[7], statb;

	/* read twice until the snapshot is stable */
	for (int tries = 0; tries < 4; tries++) {
		rtc_wait_ready();
		a[0] = rtc_cmos(0x00);		/* seconds */
		a[1] = rtc_cmos(0x02);		/* minutes */
		a[2] = rtc_cmos(0x04);		/* hours */
		a[3] = rtc_cmos(0x07);		/* day of month */
		a[4] = rtc_cmos(0x08);		/* month */
		a[5] = rtc_cmos(0x09);		/* year */
		a[6] = rtc_cmos(0x06);		/* day of week (unreliable) */
		rtc_wait_ready();
		b[0] = rtc_cmos(0x00);
		b[1] = rtc_cmos(0x02);
		b[2] = rtc_cmos(0x04);
		b[3] = rtc_cmos(0x07);
		b[4] = rtc_cmos(0x08);
		b[5] = rtc_cmos(0x09);
		b[6] = rtc_cmos(0x06);
		if (memcmp(a, b, sizeof(a)) == 0)
			break;
		memcpy(a, b, sizeof(a));
	}

	statb = rtc_cmos(0x0B);
	if ((statb & 0x04) == 0) {
		for (int i = 0; i < 6; i++)
			a[i] = rtc_bcd(a[i]);
	}
	/* 12-hour mode: bit 7 of the hour byte means PM */
	if ((statb & 0x02) == 0) {
		int pm = a[2] & 0x80;

		a[2] &= 0x7F;
		if (pm && a[2] != 12)
			a[2] += 12;
		else if (!pm && a[2] == 12)
			a[2] = 0;
	}

	rt->sec = a[0];
	rt->min = a[1];
	rt->hour = a[2];
	rt->day = a[3];
	rt->mon = a[4];
	rt->year = a[5];
	rt->year += rt->year >= 70 ? 1900 : 2000;
	rt->wday = rtc_wday(rt->year, rt->mon, rt->day);
}
