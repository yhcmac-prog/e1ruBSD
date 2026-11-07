/*	$e1ruBSD: date.c,v 1.1 2026/11/07 00:00:00 e1 Exp $	*/

#include "ulib.h"

static const char *wday_name[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char *mon_name[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

int
main(int argc, char **argv)
{
	struct rtc_time rt;

	(void)argc;
	(void)argv;
	gettime(&rt);
	if (rt.wday < 0 || rt.wday > 6 || rt.mon < 1 || rt.mon > 12) {
		printf("date: RTC read failed\n");
		return 1;
	}
	/* The firmware RTC is reported as-is: no timezone conversion. */
	printf("%s %s %2d %02d:%02d:%02d %d\n",
	    wday_name[rt.wday], mon_name[rt.mon - 1], rt.day,
	    rt.hour, rt.min, rt.sec, rt.year);
	return 0;
}
