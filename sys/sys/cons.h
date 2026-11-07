/*	$e1ruBSD: cons.h,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

#ifndef _SYS_CONS_H_
#define _SYS_CONS_H_

#include <sys/types.h>

/* dev/cons.c - VGA text + COM1 console with line discipline */
void	 cons_init(void);
void	 cnputc(int c);
int	 cngetc(void);			/* blocking single char (raw) */
int	 cons_read(char *buf, size_t n);	/* line discipline read */
void	 cons_poll_serial(void);	/* pump serial RX into input queue */
void	 cons_kbd_input(u_char c);	/* IRQ1 path (pckbd.c) */

#endif	/* !_SYS_CONS_H_ */
