/*	$e1ruBSD: acpi.c,v 1.1 2026/11/07 00:00:00 e1 Exp $	*/

/*
 * Minimal ACPI support for system power control.  We do not build an AML
 * interpreter: the firmware tables are scanned for the FADT and the \_S5
 * package, which is enough to enter the soft-off state, and the FADT
 * reset register is used for a real reboot when the firmware provides one.
 *
 * Legacy PM controllers (PIIX4 style ports) remain as fallbacks in
 * sys_reboot() when the tables are missing or unusable.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <machine/cpu.h>

#define	ACPI_SLP_EN	0x2000
#define	ACPI_SLP_TYP_SHIFT 10

static int	 acpi_ok;
static uint16_t	 acpi_pm1a_cnt;
static uint16_t	 acpi_pm1b_cnt;
static uint8_t	 acpi_s5a = 5;
static uint8_t	 acpi_s5b = 5;
static uint64_t	 acpi_reset_addr;
static uint8_t	 acpi_reset_space;
static uint8_t	 acpi_reset_value;

static uint8_t
acpi_sum(const uint8_t *p, uint32_t len)
{
	uint8_t sum = 0;

	for (uint32_t i = 0; i < len; i++)
		sum = (uint8_t)(sum + p[i]);
	return sum;
}

static uint16_t
rd16(const uint8_t *p)
{
	uint16_t v;

	memcpy(&v, p, sizeof(v));
	return v;
}

static uint32_t
rd32(const uint8_t *p)
{
	uint32_t v;

	memcpy(&v, p, sizeof(v));
	return v;
}

static uint64_t
rd64(const uint8_t *p)
{
	uint64_t v;

	memcpy(&v, p, sizeof(v));
	return v;
}

/* RSDP lives either in the EBDA or in the last 1KB of the BIOS area. */
static uint64_t
acpi_find_rsdp(void)
{
	uint16_t seg = *(volatile uint16_t *)P2V(0x40E);
	uint64_t ebda = (uint64_t)seg << 4;

	for (uint64_t pa = ebda; pa < ebda + 1024; pa += 16) {
		uint8_t *p = (uint8_t *)P2V(pa);

		if (memcmp(p, "RSD PTR ", 8) == 0 && acpi_sum(p, 20) == 0)
			return pa;
	}
	for (uint64_t pa = 0xE0000; pa < 0x100000; pa += 16) {
		uint8_t *p = (uint8_t *)P2V(pa);

		if (memcmp(p, "RSD PTR ", 8) == 0 && acpi_sum(p, 20) == 0)
			return pa;
	}
	return 0;
}

/* Walk the RSDT/XSDT and return the table body with the wanted signature. */
static uint8_t *
acpi_find_table(const uint8_t *rsdp, const char *sig)
{
	uint32_t rsdt = rd32(rsdp + 16);
	uint64_t xsdt = 0;

	if (rsdp[15] >= 2)
		xsdt = rd64(rsdp + 24);

	if (xsdt != 0) {
		uint8_t *t = (uint8_t *)P2V(xsdt);
		uint32_t len = rd32(t + 4);

		if (len >= 36 && acpi_sum(t, len) == 0) {
			for (uint32_t off = 36; off + 8 <= len; off += 8) {
				uint8_t *h = (uint8_t *)P2V(rd64(t + off));

				if (memcmp(h, sig, 4) == 0)
					return h;
			}
		}
	}
	if (rsdt != 0) {
		uint8_t *t = (uint8_t *)P2V(rsdt);
		uint32_t len = rd32(t + 4);

		if (len >= 36 && acpi_sum(t, len) == 0) {
			for (uint32_t off = 36; off + 4 <= len; off += 4) {
				uint8_t *h = (uint8_t *)P2V(rd32(t + off));

				if (memcmp(h, sig, 4) == 0)
					return h;
			}
		}
	}
	return NULL;
}

/*
 * Extract \_S5 from the DSDT AML.  Bytecode layout:
 *   NameOp(0x08) "_S5_" PackageOp(0x12) PkgLength NumElements elem elem
 * where each element is a small integer constant (0x00, 0x01, 0x0A nn,
 * 0x0B nn nn, ...).
 */
static int
acpi_parse_s5(uint64_t dsdt_pa, uint32_t len)
{
	uint8_t *d = (uint8_t *)P2V(dsdt_pa);

	if (len < 8 || acpi_sum(d, len) != 0)
		return -1;
	for (uint32_t i = 1; i + 4 < len; i++) {
		uint32_t p;
		uint8_t lead, vals[2];

		if (d[i - 1] != 0x08 || d[i] != '_' || d[i + 1] != 'S' ||
		    d[i + 2] != '5' || d[i + 3] != '_')
			continue;
		p = i + 4;
		if (d[p] != 0x12)
			continue;
		p++;
		lead = d[p];
		p++;
		p += lead >> 6;			/* remaining PkgLength bytes */
		p++;				/* NumElements */
		vals[0] = vals[1] = 0;
		for (int k = 0; k < 2; k++) {
			if (p >= len)
				return -1;
			switch (d[p]) {
			case 0x00:
			case 0x01:
				vals[k] = d[p];
				p += 1;
				break;
			case 0x0A:
				if (p + 1 >= len)
					return -1;
				vals[k] = d[p + 1];
				p += 2;
				break;
			case 0x0B:
				if (p + 2 >= len)
					return -1;
				vals[k] = (uint8_t)rd16(d + p + 1);
				p += 3;
				break;
			case 0x0C:
				if (p + 4 >= len)
					return -1;
				vals[k] = (uint8_t)rd32(d + p + 1);
				p += 5;
				break;
			default:
				return -1;
			}
		}
		acpi_s5a = vals[0];
		acpi_s5b = vals[1];
		return 0;
	}
	return -1;
}

int
acpi_init(void)
{
	uint64_t rsdp_pa = acpi_find_rsdp();
	uint8_t *rsdp, *fadt;
	uint32_t fadt_len, dsdt = 0;

	if (rsdp_pa == 0) {
		kprintf("acpi0 at mainbus: no RSDP (legacy power control)\n");
		return 0;
	}
	rsdp = (uint8_t *)P2V(rsdp_pa);
	fadt = acpi_find_table(rsdp, "FACP");
	if (fadt == NULL) {
		kprintf("acpi0 at mainbus: no FADT (legacy power control)\n");
		return 0;
	}
	fadt_len = rd32(fadt + 4);
	if (fadt_len < 116 || acpi_sum(fadt, fadt_len) != 0) {
		kprintf("acpi0 at mainbus: bad FADT\n");
		return 0;
	}

	acpi_pm1a_cnt = (uint16_t)rd32(fadt + 64);
	acpi_pm1b_cnt = (uint16_t)rd32(fadt + 68);
	dsdt = rd32(fadt + 40);
	if (dsdt == 0 && fadt_len >= 148)
		dsdt = (uint32_t)rd64(fadt + 140);

	/* ACPI 2.0+ reset register, present when FADT flags bit 10 is set */
	if (fadt_len >= 129 && (rd32(fadt + 112) & 0x400)) {
		acpi_reset_addr = rd64(fadt + 116);
		acpi_reset_space = fadt[124];
		acpi_reset_value = fadt[128];
	}

	if (dsdt != 0) {
		uint32_t dlen = rd32((uint8_t *)P2V(dsdt) + 4);

		if (acpi_parse_s5(dsdt, dlen) != 0)
			kprintf("acpi0: no usable \\_S5 package\n");
	}
	if (acpi_pm1a_cnt == 0) {
		kprintf("acpi0 at mainbus: no PM1a control block\n");
		return 0;
	}
	acpi_ok = 1;
	kprintf("acpi0 at mainbus: FADT rev %u, PM1a 0x%x PM1b 0x%x, "
	    "S5 %u/%u\n", fadt[8], (unsigned)acpi_pm1a_cnt,
	    (unsigned)acpi_pm1b_cnt, (unsigned)acpi_s5a,
	    (unsigned)acpi_s5b);
	return 1;
}

/* Enter S5 (soft off).  Returns 0 when ACPI is not available. */
int
acpi_poweroff(void)
{
	if (!acpi_ok)
		return 0;
	outw(acpi_pm1a_cnt,
	    (uint16_t)((acpi_s5a << ACPI_SLP_TYP_SHIFT) | ACPI_SLP_EN));
	if (acpi_pm1b_cnt != 0)
		outw(acpi_pm1b_cnt,
		    (uint16_t)((acpi_s5b << ACPI_SLP_TYP_SHIFT) | ACPI_SLP_EN));
	io_wait();
	return 1;
}

/* Reset through the FADT reset register.  Returns 0 if none is present. */
int
acpi_reboot(void)
{
	if (!acpi_ok || acpi_reset_addr == 0)
		return 0;
	if (acpi_reset_space == 1) {
		outb((uint16_t)acpi_reset_addr, acpi_reset_value);
		io_wait();
		return 1;
	}
	return 0;
}
