/*	$e1ruBSD: device.h,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#ifndef _SYS_DEVICE_H_
#define _SYS_DEVICE_H_

#include <sys/types.h>

/*
 * Lightweight autoconf(9)-style device model: a static config table walks
 * the mainbus and calls each driver's attach function, NetBSD style.
 */

struct device {
	const char	*dv_name;
	int		 dv_unit;
	void		*dv_private;
};

struct cfdriver {
	const char	*cd_name;		/* e.g. "pit" */
	int		(*cd_attach)(struct device *);	/* returns 0 on success */
};

#define	CFDRIVER(name, attachfn) \
	const struct cfdriver __cfdrv_##name = { #name, attachfn }

void	 autoconf(void);		/* attach the world, print dmesg lines */
void	 device_attached(const char *bus, struct device *dv,
	     const char *info);

#endif	/* !_SYS_DEVICE_H_ */
