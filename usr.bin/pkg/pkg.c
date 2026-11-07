/*	$e1ruBSD: pkg.c,v 1.1 2026/11/07 00:00:00 e1 Exp $	*/

/*
 * e1pkg: local package manager.  Packages are ustar archives stored in
 * /packages; the first member is a plain-text manifest:
 *
 *	 name=hello
 *	 version=1.0
 *	 desc=demo package
 *	 exec=/bin/hello
 *
 * Remaining members are installed relative to "/", and the list of
 * installed paths is kept in /var/db/e1pkg/<name>.  The root filesystem
 * is ramfs, so the database and installed files do not survive a reboot.
 */

#include "ulib.h"

#define	PKG_DIR	"/packages"
#define	DB_DIR	"/var/db/e1pkg"
#define	MAX_FILE	(256 * 1024)
#define	MAX_PATHS	32
#define	MAX_EXECS	8

static char	pkgbuf[MAX_FILE];
static char	pathbuf[128];
static char	dbuf[8192];

static int
read_file(const char *path, char *buf, int max)
{
	int fd = open(path, O_RDONLY);
	int total = 0;

	if (fd < 0)
		return -1;
	while (total < max) {
		unsigned long want = (unsigned long)(max - total);
		long n;

		/* sys_read() rejects requests larger than 64KB */
		if (want > 32768)
			want = 32768;
		n = read(fd, buf + total, want);
		if (n <= 0)
			break;
		total += (int)n;
	}
	close(fd);
	return total;
}

static void
ensure_dir(const char *path)
{
	char buf[128];

	if (strlen(path) >= sizeof(buf))
		return;
	strlcpy(buf, path, sizeof(buf));
	for (char *p = buf + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (buf[0] != '\0')
			mkdir(buf);
		*p = '/';
	}
	if (buf[0] != '\0')
		mkdir(buf);
}

static void
dir_of(const char *path, char *out, size_t len)
{
	char buf[128];
	char *slash = NULL;

	strlcpy(buf, path, sizeof(buf));
	for (char *p = buf; *p; p++)
		if (*p == '/')
			slash = p;
	if (slash == NULL) {
		strlcpy(out, ".", len);
		return;
	}
	if (slash == buf)
		slash[1] = '\0';
	else
		*slash = '\0';
	strlcpy(out, buf, len);
}

static int
write_file(const char *path, const char *data, int len, unsigned mode)
{
	char dir[128];
	int fd, put = 0;

	dir_of(path, dir, sizeof(dir));
	ensure_dir(dir);
	fd = open(path, O_CREAT | O_TRUNC | O_WRONLY);
	if (fd < 0)
		return -1;
	while (put < len) {
		unsigned long want = (unsigned long)(len - put);
		long n;

		if (want > 32768)
			want = 32768;
		n = write(fd, data + put, want);
		if (n <= 0)
			break;
		put += (int)n;
	}
	close(fd);
	if (put != len)
		return -1;
	chmod(path, mode);
	return 0;
}

/* bounded string append used for the installed-file database */
static int
append(char *buf, int len, int max, const char *s)
{
	while (*s && len < max - 1)
		buf[len++] = *s++;
	buf[len] = '\0';
	return len;
}

static int
ends_with(const char *s, const char *suf)
{
	size_t a = strlen(s), b = strlen(suf);

	return a >= b && strcmp(s + a - b, suf) == 0;
}

/* ---------------- ustar helpers ---------------- */

static unsigned long
tar_octal(const char *p, int n)
{
	unsigned long v = 0;

	while (n > 0 && (*p == ' ' || *p == '0'))
		p++, n--;
	while (n > 0 && *p >= '0' && *p <= '7') {
		v = v * 8 + (unsigned long)(*p - '0');
		p++, n--;
	}
	return v;
}

static void
tar_name(const char *h, char *out, size_t len)
{
	size_t n = 0;

	while (n < 100 && h[n] != '\0') {
		if (n + 1 < len)
			out[n] = h[n];
		n++;
	}
	out[n < len ? n : len - 1] = '\0';
}

static void
manifest_get(const char *data, int len, char *name, size_t nlen, char *ver,
    size_t vlen, char *desc, size_t dlen, char execs[][128], int *nexecs)
{
	int i = 0;

	while (i < len) {
		int start = i, l;

		while (i < len && data[i] != '\n')
			i++;
		l = i - start;
		if (l > 5 && strncmp(data + start, "name=", 5) == 0)
			strlcpy(name, data + start + 5,
			    (size_t)l - 4 < nlen ? (size_t)l - 4 : nlen);
		else if (l > 8 && strncmp(data + start, "version=", 8) == 0)
			strlcpy(ver, data + start + 8,
			    (size_t)l - 7 < vlen ? (size_t)l - 7 : vlen);
		else if (l > 5 && strncmp(data + start, "desc=", 5) == 0)
			strlcpy(desc, data + start + 5,
			    (size_t)l - 4 < dlen ? (size_t)l - 4 : dlen);
		else if (l > 5 && strncmp(data + start, "exec=", 5) == 0 &&
		    *nexecs < MAX_EXECS)
			strlcpy(execs[(*nexecs)++], data + start + 5,
			    (size_t)l - 4 < 128 ? (size_t)l - 4 : 128);
		if (i < len)
			i++;
	}
}

/* ---------------- commands ---------------- */

static int
pkg_install(const char *arg)
{
	char name[64] = "", ver[32] = "1.0", desc[96] = "";
	char files[MAX_PATHS][128], execs[MAX_EXECS][128];
	int nfiles = 0, nexecs = 0, total, off = 0;

	if (arg[0] == '/') {
		strlcpy(pathbuf, arg, sizeof(pathbuf));
	} else {
		strlcpy(pathbuf, PKG_DIR "/", sizeof(pathbuf));
		strlcpy(pathbuf + strlen(pathbuf), arg, sizeof(pathbuf) -
		    strlen(pathbuf));
		if (!ends_with(arg, ".pkg"))
			strlcpy(pathbuf + strlen(pathbuf), ".pkg",
			    sizeof(pathbuf) - strlen(pathbuf));
	}
	total = read_file(pathbuf, pkgbuf, sizeof(pkgbuf));
	if (total <= 0) {
		printf("pkg: %s: package not found\n", pathbuf);
		return 1;
	}
	while (off + 512 <= total) {
		char *h = pkgbuf + off;
		char namebuf[101];
		unsigned long sz;

		if (h[0] == '\0')
			break;
		sz = tar_octal(h + 124, 12);
		tar_name(h, namebuf, sizeof(namebuf));
		off += 512;
		if (off > total)
			break;
		if (h[156] == '5') {
			if (namebuf[0] != '\0') {
				strlcpy(pathbuf, "/", sizeof(pathbuf));
				strlcpy(pathbuf + 1, namebuf,
				    sizeof(pathbuf) - 1);
				ensure_dir(pathbuf);
			}
		} else if (h[156] == '0' || h[156] == '\0') {
			unsigned mode = (unsigned)tar_octal(h + 100, 8);

			if (strcmp(namebuf, "manifest") == 0) {
				manifest_get(pkgbuf + off, (int)sz, name,
				    sizeof(name), ver, sizeof(ver), desc,
				    sizeof(desc), execs, &nexecs);
			} else if (namebuf[0] != '\0') {
				strlcpy(pathbuf, "/", sizeof(pathbuf));
				strlcpy(pathbuf + 1, namebuf,
				    sizeof(pathbuf) - 1);
				if (write_file(pathbuf, pkgbuf + off,
				    (int)sz, mode) == 0 && nfiles < MAX_PATHS)
					strlcpy(files[nfiles++], pathbuf,
					    sizeof(files[0]));
			else
				printf("pkg: %s: write failed\n",
				    pathbuf);
			}
		}
		off += (int)((sz + 511) & ~511UL);
	}
	if (name[0] == '\0') {
		printf("pkg: %s: missing or invalid manifest\n", pathbuf);
		return 1;
	}
	/* executables are flagged by manifest lines */
	for (int i = 0; i < nexecs; i++) {
		if (execs[i][0] == '/')
			chmod(execs[i], 0755);
		else {
			char ep[128];

			strlcpy(ep, "/", sizeof(ep));
			strlcpy(ep + 1, execs[i], sizeof(ep) - 1);
			chmod(ep, 0755);
		}
	}
	/* installed-file database */
	ensure_dir(DB_DIR);
	strlcpy(pathbuf, DB_DIR "/", sizeof(pathbuf));
	strlcpy(pathbuf + strlen(pathbuf), name, sizeof(pathbuf) -
	    strlen(pathbuf));
	{
		char rec[8192];
		int n = 0;

		n = append(rec, n, sizeof(rec), "name=");
		n = append(rec, n, sizeof(rec), name);
		n = append(rec, n, sizeof(rec), "\nversion=");
		n = append(rec, n, sizeof(rec), ver);
		n = append(rec, n, sizeof(rec), "\ndesc=");
		n = append(rec, n, sizeof(rec), desc);
		n = append(rec, n, sizeof(rec), "\n");
		for (int i = 0; i < nfiles && n < (int)sizeof(rec) - 140; i++) {
			n = append(rec, n, sizeof(rec), "file=");
			n = append(rec, n, sizeof(rec), files[i]);
			n = append(rec, n, sizeof(rec), "\n");
		}
		write_file(pathbuf, rec, n, 0644);
	}
	printf("pkg: installed %s-%s (%d files)\n", name, ver, nfiles);
	printf("pkg: note: rootfs is ramfs, reinstall after reboot\n");
	return 0;
}

/* remove now-empty parent directories, stopping at the first failure */
static void
prune_dirs(const char *file)
{
	char dir[128];

	strlcpy(dir, file, sizeof(dir));
	for (;;) {
		char *slash = NULL;

		for (char *p = dir; *p; p++)
			if (*p == '/')
				slash = p;
		if (slash == NULL || slash == dir)
			break;
		*slash = '\0';
		if (rmdir(dir) != 0)
			break;
	}
}

static int
pkg_remove(const char *name)
{
	int total, i = 0, removed = 0;

	if (name[0] == '/') {
		strlcpy(pathbuf, name, sizeof(pathbuf));
	} else {
		strlcpy(pathbuf, DB_DIR "/", sizeof(pathbuf));
		strlcpy(pathbuf + strlen(pathbuf), name, sizeof(pathbuf) -
		    strlen(pathbuf));
	}
	total = read_file(pathbuf, dbuf, sizeof(dbuf));
	if (total <= 0) {
		printf("pkg: %s is not installed\n", name);
		return 1;
	}
	while (i < total) {
		int start = i, l;

		while (i < total && dbuf[i] != '\n')
			i++;
		l = i - start;
		if (l > 5 && strncmp(dbuf + start, "file=", 5) == 0) {
			char fp[128];

			strlcpy(fp, dbuf + start + 5,
			    (size_t)l - 4 < sizeof(fp) ? (size_t)l - 4 :
			    sizeof(fp));
			if (unlink(fp) == 0) {
				removed++;
				prune_dirs(fp);
			}
		}
		if (i < total)
			i++;
	}
	unlink(pathbuf);
	printf("pkg: removed %s (%d files)\n", name, removed);
	return 0;
}

static int
pkg_list(void)
{
	static char rbuf[16384];
	int n = readdir(DB_DIR, rbuf, sizeof(rbuf));
	int shown = 0;

	if (n <= 0) {
		printf("pkg: no packages installed\n");
		return 0;
	}
	struct dirent *de = (struct dirent *)rbuf;

	for (int i = 0; i < n; i++) {
		char rec[512];
		int total;
		char name[64] = "", ver[32] = "", desc[96] = "";
		int files = 0;

		if (de[i].d_name[0] == '\0' || de[i].d_type == DT_DIR)
			continue;
		strlcpy(pathbuf, DB_DIR "/", sizeof(pathbuf));
		strlcpy(pathbuf + strlen(pathbuf), de[i].d_name,
		    sizeof(pathbuf) - strlen(pathbuf));
		total = read_file(pathbuf, rec, sizeof(rec));
		if (total <= 0)
			continue;
		for (int p = 0; p < total; ) {
			int start = p, l;

			while (p < total && rec[p] != '\n')
				p++;
			l = p - start;
			if (l > 8 && strncmp(rec + start, "version=", 8) == 0)
				strlcpy(ver, rec + start + 8,
				    (size_t)l - 7 < sizeof(ver) ?
				    (size_t)l - 7 : sizeof(ver));
			else if (l > 5 &&
			    strncmp(rec + start, "desc=", 5) == 0)
				strlcpy(desc, rec + start + 5,
				    (size_t)l - 4 < sizeof(desc) ?
				    (size_t)l - 4 : sizeof(desc));
			else if (l > 5 &&
			    strncmp(rec + start, "name=", 5) == 0)
				strlcpy(name, rec + start + 5,
				    (size_t)l - 4 < sizeof(name) ?
				    (size_t)l - 4 : sizeof(name));
			else if (l > 5 &&
			    strncmp(rec + start, "file=", 5) == 0)
				files++;
			if (p < total)
				p++;
		}
		if (name[0] == '\0')
			strlcpy(name, de[i].d_name, sizeof(name));
		printf("%s %s - %s (%d files)\n", name, ver, desc, files);
		shown++;
	}
	if (shown == 0)
		printf("pkg: no packages installed\n");
	return 0;
}

static int
pkg_available(void)
{
	static char rbuf[16384];
	int n = readdir(PKG_DIR, rbuf, sizeof(rbuf));

	if (n <= 0) {
		printf("pkg: no package repository\n");
		return 0;
	}
	struct dirent *de = (struct dirent *)rbuf;

	for (int i = 0; i < n; i++) {
		char nm[64];

		if (de[i].d_name[0] == '\0' || de[i].d_type == DT_DIR)
			continue;
		if (!ends_with(de[i].d_name, ".pkg"))
			continue;
		strlcpy(nm, de[i].d_name, sizeof(nm));
		nm[strlen(nm) - 4] = '\0';
		printf("%s\n", nm);
	}
	return 0;
}

int
main(int argc, char **argv)
{
	if (argc < 2) {
		printf("usage: pkg install|remove|list|available [name]\n");
		return 1;
	}
	if (strcmp(argv[1], "install") == 0) {
		if (argc < 3) {
			printf("usage: pkg install name\n");
			return 1;
		}
		return pkg_install(argv[2]);
	}
	if (strcmp(argv[1], "remove") == 0) {
		if (argc < 3) {
			printf("usage: pkg remove name\n");
			return 1;
		}
		return pkg_remove(argv[2]);
	}
	if (strcmp(argv[1], "list") == 0)
		return pkg_list();
	if (strcmp(argv[1], "available") == 0)
		return pkg_available();
	printf("pkg: unknown command %s\n", argv[1]);
	return 1;
}
