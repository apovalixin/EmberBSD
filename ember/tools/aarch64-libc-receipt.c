/* Origin: EmberBSD; AI-assisted private full-libc runtime binding receipt. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define _NETBSD_SOURCE 1
#include <sys/types.h>
#include <sys/stat.h>

#include <dlfcn.h>
#include <inttypes.h>
#include <link_elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct stat expected;
static unsigned int libc_count, errors;
static const char *phase;

static int
mapping(struct dl_phdr_info *info, size_t size, void *data)
{
	struct stat bound;
	const char *name, *base;

	(void)size;
	(void)data;
	name = info->dlpi_name;
	if (name == NULL || name[0] == '\0')
		return 0;
	if (stat(name, &bound) != 0) {
		fprintf(stderr, "MAP-FAIL pid=%jd phase=%s file=%s\n",
		    (intmax_t)getpid(), phase, name);
		errors++;
		return 0;
	}
	fprintf(stderr, "MAP pid=%jd phase=%s file=%s base=%#jx "
	    "dev=%ju ino=%ju size=%jd\n", (intmax_t)getpid(), phase, name,
	    (uintmax_t)info->dlpi_addr, (uintmax_t)bound.st_dev,
	    (uintmax_t)bound.st_ino, (intmax_t)bound.st_size);
	base = strrchr(name, '/');
	base = base != NULL ? base + 1 : name;
	if (strcmp(base, "libc.so") == 0 || strncmp(base, "libc.so.", 8) == 0) {
		libc_count++;
		if (bound.st_dev != expected.st_dev ||
		    bound.st_ino != expected.st_ino)
			errors++;
	}
	return 0;
}

static void
receipt(const char *stage)
{
	Dl_info provider;
	struct stat bound;
	void *address;

	phase = stage;
	libc_count = errors = 0;
	(void)dl_iterate_phdr(mapping, NULL);
	/* A C function address can name the executable's canonical PLT entry. */
	address = dlsym(RTLD_DEFAULT, "malloc");
	if (address == NULL || dladdr(address, &provider) == 0 ||
	    provider.dli_fname == NULL || stat(provider.dli_fname, &bound) != 0 ||
	    bound.st_dev != expected.st_dev || bound.st_ino != expected.st_ino)
		errors++;
	else
		fprintf(stderr, "LIBC-BIND pid=%jd phase=%s symbol=malloc "
		    "address=%p provider=%s dev=%ju ino=%ju\n", (intmax_t)getpid(),
		    phase, address, provider.dli_fname, (uintmax_t)bound.st_dev,
		    (uintmax_t)bound.st_ino);
	fprintf(stderr, "LIBC-RECEIPT pid=%jd phase=%s count=%u errors=%u\n",
	    (intmax_t)getpid(), phase, libc_count, errors);
	if (libc_count != 1 || errors != 0)
		_exit(125);
}

static void __attribute__((constructor))
start(void)
{
	const char *path = getenv("EMBER_LIBC_EXPECT");

	if (path == NULL || path[0] != '/' || lstat(path, &expected) != 0 ||
	    !S_ISREG(expected.st_mode)) {
		fprintf(stderr, "LIBC-RECEIPT invalid EMBER_LIBC_EXPECT\n");
		_exit(125);
	}
	receipt("start");
}

static void __attribute__((destructor))
finish(void)
{
	receipt("finish");
}

#ifdef EMBER_LIBC_RECEIPT_MAIN
int
main(void)
{
	unsigned char *p, *grown;
	FILE *stream;
	char line[32];
	unsigned int i;

	p = calloc(4096, 1);
	if (p == NULL)
		return 1;
	for (i = 0; i < 4096; i++) {
		if (p[i] != 0)
			return 1;
		p[i] = (unsigned char)i;
	}
	grown = realloc(p, 8192);
	if (grown == NULL) {
		free(p);
		return 1;
	}
	for (i = 0; i < 4096; i++) {
		if (grown[i] != (unsigned char)i)
			return 1;
	}
	free(grown);
	stream = tmpfile();
	if (stream == NULL)
		return 1;
	if (fputs("libc smoke\n", stream) == EOF || fflush(stream) != 0)
		return 1;
	rewind(stream);
	if (fgets(line, sizeof(line), stream) == NULL ||
	    strcmp(line, "libc smoke\n") != 0 || fclose(stream) != 0)
		return 1;
	puts("PASS bounded allocator/string/stdio smoke");
	return 0;
}
#endif
