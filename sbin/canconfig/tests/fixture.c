/* Origin: EmberBSD; AI-assisted production canconfig command regression. */
/*
 * Copyright (c) 2026 EmberBSD contributors
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Only the system-call boundary is modeled here.  The included source is
 * the production command parser and printer.  Native rump tests separately
 * exercise the real CAN socket and interface ioctls.
 */
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <netcan/can.h>
#include <netcan/can_link.h>
#include <ifaddrs.h>

#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fixture_socket(int, int, int);
static int fixture_ioctl(int, unsigned long, ...);
static int fixture_getifaddrs(struct ifaddrs **);
static void fixture_freeifaddrs(struct ifaddrs *);
__dead static void fixture_exit(int);

#define socket fixture_socket
#define ioctl fixture_ioctl
#define getifaddrs fixture_getifaddrs
#define freeifaddrs fixture_freeifaddrs
#define exit fixture_exit
#define main canconfig_main
#include "../canconfig.c"
#undef main
#undef exit
#undef freeifaddrs
#undef getifaddrs
#undef ioctl
#undef socket

static struct can_link_timecaps caps;
static struct can_link_timings timings;
static struct ifaddrs address;
static uint32_t mode;
static int flags;

static unsigned long
env_number(const char *name, unsigned long fallback)
{
	const char *value;
	char *end;
	unsigned long number;

	value = getenv(name);
	if (value == NULL)
		return fallback;
	errno = 0;
	number = strtoul(value, &end, 0);
	if (errno != 0 || end == value || *end != '\0')
		errx(99, "invalid fixture environment: %s", name);
	return number;
}

static void
expect(const char *name, unsigned long actual)
{

	if (env_number(name, actual) != actual)
		errx(99, "%s: actual state %lu", name, actual);
}

__dead static void
fixture_exit(int status)
{

	if (status == 0) {
		expect("EXPECT_MODE", mode);
		expect("EXPECT_FLAGS", flags);
		expect("EXPECT_BRP", timings.clt_brp);
		expect("EXPECT_PROP", timings.clt_prop);
		expect("EXPECT_PS1", timings.clt_ps1);
		expect("EXPECT_PS2", timings.clt_ps2);
		expect("EXPECT_SJW", timings.clt_sjw);
	}
	exit(status);
}

static int
fixture_socket(int domain, int type, int protocol)
{

	if (domain != AF_CAN || type != SOCK_RAW || protocol != CAN_RAW)
		errx(99, "unexpected socket request");
	return 7;
}

static int
fixture_getifaddrs(struct ifaddrs **result)
{

	address.ifa_name = __UNCONST("canlo0");
	*result = &address;
	return 0;
}

static void
fixture_freeifaddrs(struct ifaddrs *result)
{

	if (result != &address)
		errx(99, "unexpected address list");
}

static int
fixture_ioctl(int fd, unsigned long request, ...)
{
	struct ifreq *ifr;
	struct ifdrv *ifd;
	uint32_t value;
	void *data;
	va_list ap;

	if (fd != 7)
		errx(99, "unexpected socket descriptor");
	va_start(ap, request);
	data = va_arg(ap, void *);
	va_end(ap);
	ifr = data;
	if (strcmp(ifr->ifr_name, "canlo0") != 0) {
		errno = ENXIO;
		return -1;
	}
	switch (request) {
	case SIOCGIFFLAGS:
		ifr->ifr_flags = flags;
		return 0;
	case SIOCSIFFLAGS:
		flags = ifr->ifr_flags;
		return 0;
	case SIOCGIFMTU:
		ifr->ifr_mtu = (mode & CAN_LINKMODE_FD) ? CANFD_MTU : CAN_MTU;
		return 0;
	case SIOCGDRVSPEC:
	case SIOCSDRVSPEC:
		break;
	default:
		errx(99, "unexpected ioctl request %lu", request);
	}
	ifd = data;
	if (request == SIOCGDRVSPEC) {
		switch (ifd->ifd_cmd) {
		case CANGLINKTIMECAP:
			memcpy(ifd->ifd_data, &caps, sizeof(caps));
			return 0;
		case CANGLINKTIMINGS:
			memcpy(ifd->ifd_data, &timings, sizeof(timings));
			return 0;
		case CANGLINKMODE:
			memcpy(ifd->ifd_data, &mode, sizeof(mode));
			return 0;
		}
	} else {
		if (getenv("FIXTURE_FAIL_SET") != NULL) {
			errno = EIO;
			return -1;
		}
		if ((flags & IFF_UP) != 0) {
			errno = EBUSY;
			return -1;
		}
		switch (ifd->ifd_cmd) {
		case CANSLINKTIMINGS:
			memcpy(&timings, ifd->ifd_data, sizeof(timings));
			return 0;
		case CANSLINKMODE:
		case CANCLINKMODE:
			memcpy(&value, ifd->ifd_data, sizeof(value));
			if ((value & caps.cltc_linkmode_caps) != value) {
				errno = EINVAL;
				return -1;
			}
			if (ifd->ifd_cmd == CANSLINKMODE)
				mode |= value;
			else
				mode &= ~value;
			return 0;
		}
	}
	errno = EOPNOTSUPP;
	return -1;
}

int
main(int argc, char **argv)
{
	const char *kind;

	alarm(10);
	mode = env_number("FIXTURE_MODE", 0);
	flags = env_number("FIXTURE_FLAGS", 0);
	kind = getenv("FIXTURE_KIND");
	if (kind == NULL || strcmp(kind, "virtual") == 0) {
		caps.cltc_linkmode_caps = CAN_LINKMODE_FD;
	} else {
		caps.cltc_linkmode_caps = CAN_LINKMODE_LOOPBACK |
		    CAN_LINKMODE_LISTENONLY | CAN_LINKMODE_3SAMPLES;
		caps.cltc_clock_freq = 16000000;
		caps.cltc_brp_min = 1;
		caps.cltc_brp_max = 64;
		caps.cltc_brp_inc = 1;
		caps.cltc_prop_min = caps.cltc_ps1_min = caps.cltc_ps2_min = 1;
		caps.cltc_prop_max = caps.cltc_ps1_max = caps.cltc_ps2_max = 8;
		caps.cltc_sjw_max = 4;
		timings.clt_brp = strcmp(kind, "zero-brp") == 0 ? 0 : 2;
		timings.clt_prop = 1;
		timings.clt_ps1 = 2;
		timings.clt_ps2 = 4;
		timings.clt_sjw = 1;
	}
	return canconfig_main(argc, argv);
}
