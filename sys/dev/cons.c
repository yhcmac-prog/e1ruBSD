/*	$e1ruBSD: cons.c,v 1.1 2026/10/18 00:00:00 e1 Exp $	*/

/*
 * Console: 80x25 VGA text + COM1 serial (both directions).  Output is
 * mirrored; input comes from the PS/2 keyboard interrupt and the polled
 * serial port.  A small line discipline handles echo and backspace.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cons.h>
#include <machine/cpu.h>
#include <machine/pmap.h>

#define	COLS		80
#define	LINES		25
#define	VGA_CGA		0xB8000

#define	COM1		0x3F8
#define	COM_DATA	0
#define	COM_IER		1
#define	COM_FCR		2
#define	COM_LCR		3
#define	COM_MCR		4
#define	COM_LSR		5
#define	COM_DLL		0
#define	COM_DLM		1

static int cur_row, cur_col;
static int cur_attr = 0x07;

static void com_putc(int c);

/* input ring */
#define	RING_SZ		256
static volatile u_char ring[RING_SZ];
static volatile int ring_head, ring_tail;

static void
vga_update_cursor(void)
{
	int pos = cur_row * COLS + cur_col;

	outb(0x3D4, 0x0F);
	outb(0x3D5, pos & 0xFF);
	outb(0x3D4, 0x0E);
	outb(0x3D5, (pos >> 8) & 0xFF);
}

static void
vga_scroll(void)
{
	uint16_t *fb = (uint16_t *)P2V(VGA_CGA);

	memcpy(fb, fb + COLS, (LINES - 1) * COLS * 2);
	for (int i = (LINES - 1) * COLS; i < LINES * COLS; i++)
		fb[i] = 0x0700;
}

static void
vga_putc(int c)
{
	uint16_t *fb = (uint16_t *)P2V(VGA_CGA);

	switch (c) {
	case '\n':
		cur_col = 0;
		cur_row++;
		break;
	case '\r':
		cur_col = 0;
		break;
	case '\b':
		if (cur_col > 0)
			cur_col--;
		fb[cur_row * COLS + cur_col] = (cur_attr << 8) | ' ';
		break;
	case '\t':
		cur_col = (cur_col + 8) & ~7;
		break;
	case '\f':
		for (int i = 0; i < LINES * COLS; i++)
			fb[i] = 0x0700;
		cur_row = cur_col = 0;
		break;
	default:
		fb[cur_row * COLS + cur_col] = (cur_attr << 8) | (u_char)c;
		cur_col++;
		break;
	}
	if (cur_col >= COLS) {
		cur_col = 0;
		cur_row++;
	}
	if (cur_row >= LINES) {
		cur_row = LINES - 1;
		vga_scroll();
	}
	vga_update_cursor();
}

static int
com_rx_ready(void)
{
	return inb(COM1 + COM_LSR) & 0x01;
}

void
cons_init(void)
{
	/* serial: 115200 8N1, FIFO on */
	outb(COM1 + COM_IER, 0x00);
	outb(COM1 + COM_DLL, 0x01);		/* divisor 1 = 115200 */
	outb(COM1 + COM_DLM, 0x00);
	outb(COM1 + COM_LCR, 0x03);
	outb(COM1 + COM_FCR, 0xC7);
	outb(COM1 + COM_MCR, 0x0B);

	cur_row = cur_col = 0;
	vga_putc('\f');
}

void
cnputc(int c)
{
	if (c == '\n') {
		vga_putc('\n');
		com_putc('\r');
		com_putc('\n');
	} else if (c == '\b') {
		vga_putc('\b');
		com_putc('\b');
		com_putc(' ');
		com_putc('\b');
	} else {
		vga_putc(c);
		com_putc(c);
	}
}

void
com_putc(int c)
{
	int n = 100000;

	while (!(inb(COM1 + COM_LSR) & 0x20) && n--)
		;
	outb(COM1 + COM_DATA, (u_char)c);
}

void
cons_poll_serial(void)
{
	while (com_rx_ready()) {
		u_char c = inb(COM1 + COM_DATA);
		int nt = (ring_tail + 1) % RING_SZ;

		if (nt != ring_head) {
			ring[ring_tail] = c;
			ring_tail = nt;
		}
	}
}

/* pckbd.c pushes translated keys here */
void
cons_kbd_input(u_char c)
{
	int nt = (ring_tail + 1) % RING_SZ;

	if (nt != ring_head) {
		ring[ring_tail] = c;
		ring_tail = nt;
	}
}

static int
ring_getc(void)
{
	int c = -1;

	cons_poll_serial();
	if (ring_head != ring_tail) {
		c = ring[ring_head];
		ring_head = (ring_head + 1) % RING_SZ;
	}
	return c;
}

int
cngetc(void)
{
	int c;

	do {
		c = ring_getc();
	} while (c < 0);
	return c;
}

/*
 * Line discipline: gather a line, echo characters, honor backspace,
 * return on newline or when the buffer is full.
 */
int
cons_read(char *buf, size_t n)
{
	size_t len = 0;
	int c;

	for (;;) {
		c = ring_getc();
		if (c < 0) {
			/*
			 * Nothing buffered yet: let IRQ0/IRQ1 run so the
			 * keyboard driver can refill the ring, then idle.
			 * IF is clear inside a syscall (interrupt gate).
			 */
			sti();
			hlt();
			cli();
			continue;
		}
		if (c == '\r' || c == '\n') {
			cnputc('\n');
			break;
		}
		if (c == '\b' || c == 0x7F) {
			if (len > 0) {
				len--;
				cnputc('\b');
			}
			continue;
		}
		if (c < 0x20 || c > 0x7E)
			continue;
		if (len >= n - 1)
			continue;
		buf[len++] = (char)c;
		cnputc(c);
	}
	buf[len] = '\0';
	return (int)len;
}
