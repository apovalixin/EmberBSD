/* Origin: EmberBSD; AI-assisted production outlined CAS regression. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define _NETBSD_SOURCE 1
#include <sys/types.h>
#ifdef CAS_CONTRACT_DSO
#include <sys/stat.h>

#include <dlfcn.h>
#endif

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint64_t (*cas_fn)(uint64_t, uint64_t, void *);
extern uint64_t ember_cas_call(uint64_t, uint64_t, void *, cas_fn);
#ifndef CAS_CONTRACT_DSO
#define CAS(sz, order) \
	extern uint64_t ember_cas##sz##order(uint64_t, uint64_t, void *);
#include "cas-variants.h"
#undef CAS
#endif

static struct variant {
	const char *name;
	unsigned int size;
	cas_fn fn;
} variants[] = {
#ifdef CAS_CONTRACT_DSO
#define CAS(sz, order) { "__aarch64_cas" #sz #order, sz, NULL },
#else
#define CAS(sz, order) { "cas" #sz #order, sz, ember_cas##sz##order },
#endif
#include "cas-variants.h"
#undef CAS
};

static unsigned int failures, checks;

#ifdef CAS_CONTRACT_DSO
static void *
open_provider(const char *path)
{
	struct stat requested, current, bound;
	void *handle, *address;
	const char *error;
	unsigned int i;

	if (path[0] != '/' || lstat(path, &requested) != 0 ||
	    !S_ISREG(requested.st_mode)) {
		fprintf(stderr, "Provider must be an absolute regular file: %s\n",
		    path);
		return NULL;
	}
	handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (handle == NULL) {
		fprintf(stderr, "dlopen %s: %s\n", path, dlerror());
		return NULL;
	}
	for (i = 0; i < sizeof(variants) / sizeof(variants[0]); i++) {
		Dl_info provider;

		(void)dlerror();
		address = dlsym(handle, variants[i].name);
		error = dlerror();
		if (error != NULL || address == NULL) {
			fprintf(stderr, "Missing %s in %s: %s\n", variants[i].name,
			    path, error != NULL ? error : "null address");
			goto fail;
		}
		if (dladdr(address, &provider) == 0 || provider.dli_fname == NULL ||
		    stat(provider.dli_fname, &bound) != 0 ||
		    requested.st_dev != bound.st_dev ||
		    requested.st_ino != bound.st_ino) {
			fprintf(stderr, "Wrong provider for %s in %s\n",
			    variants[i].name, path);
			goto fail;
		}
		variants[i].fn = (cas_fn)address;
		printf("BIND %s address=%p provider=%s dev=%ju ino=%ju\n",
		    variants[i].name, address, provider.dli_fname,
		    (uintmax_t)bound.st_dev, (uintmax_t)bound.st_ino);
	}
	if (lstat(path, &current) != 0 || !S_ISREG(current.st_mode) ||
	    requested.st_dev != current.st_dev ||
	    requested.st_ino != current.st_ino) {
		fprintf(stderr, "Provider changed while resolving: %s\n", path);
		goto fail;
	}
	return handle;
fail:
	(void)dlclose(handle);
	return NULL;
}
#endif

static void
check(const struct variant *v, uint64_t old, uint64_t expected,
    uint64_t desired, int match)
{
	union {
		uint64_t align;
		unsigned char bytes[24];
	} actual;
	unsigned char wanted[24];
	uint64_t returned;

	/* Native AArch64 storage uses the low bytes of the integer. */
	memset(actual.bytes, 0xa7, sizeof(actual.bytes));
	memcpy(actual.bytes + 8, &old, v->size);
	memcpy(wanted, actual.bytes, sizeof(wanted));
	if (match)
		memcpy(wanted + 8, &desired, v->size);
	returned = ember_cas_call(expected, desired, actual.bytes + 8, v->fn);
	checks++;
	if (returned != old || memcmp(actual.bytes, wanted, sizeof(wanted))) {
		failures++;
		printf("FAIL %s old=%016" PRIx64 " expected=%016" PRIx64
		    " desired=%016" PRIx64 " returned=%016" PRIx64
		    " match=%d\n", v->name, old, expected, desired, returned,
		    match);
	}
}

int
main(int argc, char **argv)
{
	const uint16_t endian = 1;
	unsigned int i, j, p, before;
	uint64_t mask, high, values[7], poisons[3], desired, old;
#ifdef CAS_CONTRACT_DSO
	void *handle;

	if (argc != 2) {
		fprintf(stderr, "usage: %s /absolute/regular/provider.so\n", argv[0]);
		return 2;
	}
	handle = open_provider(argv[1]);
	if (handle == NULL)
		return 2;
#else
	(void)argv;
	if (argc != 1)
		return 2;
#endif

	if (*(const unsigned char *)&endian != 1) {
		fprintf(stderr, "This storage fixture requires little endian.\n");
		return 2;
	}
	for (i = 0; i < sizeof(variants) / sizeof(variants[0]); i++) {
		const struct variant *v = &variants[i];

		mask = UINT64_MAX >> (64 - v->size * 8);
		high = UINT64_C(1) << (v->size * 8 - 1);
		values[0] = 0;
		values[1] = 1;
		values[2] = high - 1;
		values[3] = high;
		values[4] = mask;
		values[5] = mask - 2; /* Signed -3: original upstream failure. */
		values[6] = UINT64_C(0x123456789abcdef0) & mask;
		poisons[0] = 0;
		poisons[1] = ~mask;
		poisons[2] = UINT64_C(0xa569c37efedcba98) & ~mask;
		before = failures;
		for (j = 0; j < 7; j++) {
			old = values[j];
			for (p = 0; p < 3; p++) {
				desired = ((old - 12) & mask) | poisons[p];
				check(v, old, old | poisons[p], desired, 1);
				check(v, old, (old ^ 1) | poisons[p], desired, 0);
			}
		}
		/* CAS4 ignores high32; CAS8 must compare those same bits. */
		if (v->size >= 4)
			check(v, 3, UINT64_C(0x1234567800000003), 7,
			    v->size == 4);
		printf("%s %s\n", failures == before ? "PASS" : "FAIL",
		    v->name);
	}
	printf("%u variants, %u checks, %u failures\n", i, checks, failures);
#ifdef CAS_CONTRACT_DSO
	(void)dlclose(handle);
#endif
	return failures ? 1 : 0;
}
