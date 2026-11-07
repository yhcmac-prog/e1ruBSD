/*	$e1ruBSD: pckbd.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * PS/2 keyboard controller (8042), IRQ1.  Scancode set 1 translated to
 * ASCII with shift and capslock state; extended (0xE0) keys are dropped.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/cons.h>
#include <machine/cpu.h>

#define	KBD_DATA	0x60
#define	KBD_STATUS	0x64

static int shift, capslock;

static const char map_lower[128] = {
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
	'-', '=', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
	'o', 'p', '[', ']', '\n', 0, 'a', 's', 'd', 'f', 'g', 'h',
	'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
	'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char map_upper[128] = {
	0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
	'_', '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
	'O', 'P', '{', '}', '\n', 0, 'A', 'S', 'D', 'F', 'G', 'H',
	'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
	'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void
pckbd_intr(void)
{
	u_char sc = inb(KBD_DATA);

	if (sc & 0x80) {			/* release */
		sc &= 0x7F;
		if (sc == 0x2A || sc == 0x36)
			shift = 0;
		return;
	}
	switch (sc) {
	case 0x2A: case 0x36:			/* left/right shift */
		shift = 1;
		return;
	case 0x3A:				/* capslock */
		capslock = !capslock;
		return;
	case 0xE0:				/* extended prefix: swallow next */
		return;
	}
	if (sc >= 128)
		return;
	char c = shift ? map_upper[sc] : map_lower[sc];
	if (c == 0)
		return;
	if (c >= 'a' && c <= 'z' && capslock)
		c = c - 'a' + 'A';
	cons_kbd_input((u_char)c);
}
