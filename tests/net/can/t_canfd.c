/* Origin: EmberBSD; AI-assisted CAN FD rump socket regressions. */
/* SPDX-License-Identifier: BSD-2-Clause */
/*-
 * Copyright (c) 2026 EmberBSD contributors
 * All rights reserved.
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
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/time.h>

#include <net/if.h>
#include <netcan/can.h>
#include <netcan/can_link.h>

#include <atf-c.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

static void
set_option(int s, int option, int value)
{

	ATF_REQUIRE_MSG(rump_sys_setsockopt(s, SOL_CAN_RAW, option, &value,
	    sizeof(value)) == 0, "setsockopt(%d): %s", option, strerror(errno));
}

static int
get_option(int s, int option)
{
	int value = -1;
	socklen_t len = sizeof(value);

	ATF_REQUIRE_MSG(rump_sys_getsockopt(s, SOL_CAN_RAW, option, &value,
	    &len) == 0, "getsockopt(%d): %s", option, strerror(errno));
	ATF_REQUIRE_EQ(len, sizeof(value));
	return value;
}

static int
new_socket(bool fd)
{
	int s;

	s = rump_sys_socket(AF_CAN, SOCK_RAW, CAN_RAW);
	ATF_REQUIRE_MSG(s >= 0, "socket: %s", strerror(errno));
	if (fd)
		set_option(s, CAN_RAW_FD_FRAMES, 1);
	return s;
}

static void
close_socket(int s)
{

	ATF_REQUIRE_MSG(rump_sys_close(s) == 0, "close: %s", strerror(errno));
}

static void
ifreq_init(struct ifreq *ifr, const char *name)
{

	memset(ifr, 0, sizeof(*ifr));
	strlcpy(ifr->ifr_name, name, sizeof(ifr->ifr_name));
}

static int
create_if(int s, const char *name)
{
	struct ifreq ifr;

	ifreq_init(&ifr, name);
	ATF_REQUIRE_MSG(rump_sys_ioctl(s, SIOCIFCREATE, &ifr) == 0,
	    "create %s: %s", name, strerror(errno));
	ATF_REQUIRE_MSG(rump_sys_ioctl(s, SIOCGIFINDEX, &ifr) == 0,
	    "index %s: %s", name, strerror(errno));
	ATF_REQUIRE(ifr.ifr_ifindex > 0);
	return ifr.ifr_ifindex;
}

static void
destroy_if(int s, const char *name)
{
	struct ifreq ifr;

	ifreq_init(&ifr, name);
	ATF_REQUIRE_MSG(rump_sys_ioctl(s, SIOCIFDESTROY, &ifr) == 0,
	    "destroy %s: %s", name, strerror(errno));
}

static void
set_up(int s, const char *name, bool up)
{
	struct ifreq ifr;

	ifreq_init(&ifr, name);
	ATF_REQUIRE_EQ(rump_sys_ioctl(s, SIOCGIFFLAGS, &ifr), 0);
	if (up)
		ifr.ifr_flags |= IFF_UP;
	else
		ifr.ifr_flags &= ~IFF_UP;
	ATF_REQUIRE_MSG(rump_sys_ioctl(s, SIOCSIFFLAGS, &ifr) == 0,
	    "flags %s: %s", name, strerror(errno));
}

static int
link_ioctl(int s, const char *name, unsigned long cmd, unsigned long subcmd,
    void *data, size_t len)
{
	struct ifdrv ifd;

	memset(&ifd, 0, sizeof(ifd));
	strlcpy(ifd.ifd_name, name, sizeof(ifd.ifd_name));
	ifd.ifd_cmd = subcmd;
	ifd.ifd_len = len;
	ifd.ifd_data = data;
	return rump_sys_ioctl(s, cmd, &ifd);
}

static void
set_fd_mode(int s, const char *name, bool fd)
{
	uint32_t mode = CAN_LINKMODE_FD;

	ATF_REQUIRE_MSG(link_ioctl(s, name, SIOCSDRVSPEC,
	    fd ? CANSLINKMODE : CANCLINKMODE, &mode, sizeof(mode)) == 0,
	    "FD mode %s: %s", name, strerror(errno));
}

static uint32_t
get_mode(int s, const char *name)
{
	uint32_t mode = 0;

	ATF_REQUIRE_EQ(link_ioctl(s, name, SIOCGDRVSPEC, CANGLINKMODE,
	    &mode, sizeof(mode)), 0);
	return mode;
}

static int
get_mtu(int s, const char *name)
{
	struct ifreq ifr;

	ifreq_init(&ifr, name);
	ATF_REQUIRE_EQ(rump_sys_ioctl(s, SIOCGIFMTU, &ifr), 0);
	return ifr.ifr_mtu;
}

static int
set_mtu(int s, const char *name, int mtu)
{
	struct ifreq ifr;

	ifreq_init(&ifr, name);
	ifr.ifr_mtu = mtu;
	return rump_sys_ioctl(s, SIOCSIFMTU, &ifr);
}

static void
bind_socket(int s, int index)
{
	struct sockaddr_can sa;

	memset(&sa, 0, sizeof(sa));
	sa.can_len = sizeof(sa);
	sa.can_family = AF_CAN;
	sa.can_ifindex = index;
	ATF_REQUIRE_MSG(rump_sys_bind(s, (struct sockaddr *)&sa,
	    sizeof(sa)) == 0, "bind %d: %s", index, strerror(errno));
}

static int
create_fd_if(int s, const char *name)
{
	int index;

	index = create_if(s, name);
	set_fd_mode(s, name, true);
	set_up(s, name, true);
	return index;
}

static void
make_fd(struct canfd_frame *frame, unsigned int len, uint8_t flags)
{
	unsigned int i;

	memset(frame, 0, sizeof(*frame));
	frame->can_id = 0x123;
	frame->len = len;
	frame->flags = flags;
	/* Fill the whole record to catch truncation beyond classic CAN. */
	for (i = 0; i < sizeof(frame->data); i++)
		frame->data[i] = (i * 29 + len + 1) & 0xff;
}

static void
send_frame(int s, const void *frame, size_t len)
{
	ssize_t n;

	n = rump_sys_write(s, frame, len);
	ATF_REQUIRE_MSG(n == (ssize_t)len, "write %zu returned %zd: %s",
	    len, n, strerror(errno));
}

static void
reject_frame(int s, const void *frame, size_t len, int error)
{
	ssize_t n;

	errno = 0;
	n = rump_sys_write(s, frame, len);
	ATF_REQUIRE_MSG(n == -1 && errno == error,
	    "write %zu returned %zd, errno %d (expected %d)",
	    len, n, errno, error);
}

static int
wait_readable(int s, bool expected)
{
	struct timeval timeout;
	fd_set rfds;
	int n;

	FD_ZERO(&rfds);
	FD_SET(s, &rfds);
	/* Every receive has a deadline, including expected silence. */
	timeout.tv_sec = expected ? 2 : 0;
	timeout.tv_usec = expected ? 0 : 100000;
	n = rump_sys_select(s + 1, &rfds, NULL, NULL, &timeout);
	ATF_REQUIRE_MSG(n >= 0, "select: %s", strerror(errno));
	if (n > 0)
		ATF_REQUIRE(FD_ISSET(s, &rfds));
	return n;
}

static void
expect_none(int s)
{

	ATF_REQUIRE_MSG(wait_readable(s, false) == 0,
	    "unexpected CAN record on socket %d", s);
}

static void
receive_frame(int s, const void *expected, size_t len, int index)
{
	uint8_t frame[CANFD_MTU + 1];
	struct sockaddr_can sa;
	socklen_t salen = sizeof(sa);
	ssize_t n;

	ATF_REQUIRE_MSG(wait_readable(s, true) == 1,
	    "timed out receiving %zu bytes", len);
	memset(frame, 0xa5, sizeof(frame));
	memset(&sa, 0, sizeof(sa));
	/* Do not block if a readiness notification has become stale. */
	n = rump_sys_recvfrom(s, frame, sizeof(frame), MSG_DONTWAIT,
	    (struct sockaddr *)&sa, &salen);
	ATF_REQUIRE_MSG(n == (ssize_t)len,
	    "recvfrom returned %zd (expected %zu): %s", n, len,
	    strerror(errno));
	ATF_CHECK_EQ(salen, sizeof(sa));
	ATF_CHECK_EQ(sa.can_len, sizeof(sa));
	ATF_CHECK_EQ(sa.can_family, AF_CAN);
	ATF_CHECK_EQ(sa.can_ifindex, index);
	ATF_CHECK_MSG(memcmp(frame, expected, len) == 0,
	    "received CAN record differs from the transmitted record");
}

static void
receive_fd(int s, const struct canfd_frame *sent, int index)
{
	struct canfd_frame expected = *sent;

	expected.flags |= CANFD_FDF;
	receive_frame(s, &expected, sizeof(expected), index);
}

ATF_TC(abi);
ATF_TC_HEAD(abi, tc)
{

	atf_tc_set_md_var(tc, "descr", "classic and FD socket ABI layout");
}
ATF_TC_BODY(abi, tc)
{

	ATF_CHECK_EQ(sizeof(struct can_frame), 16);
	ATF_CHECK_EQ(CAN_MTU, 16);
	ATF_CHECK_EQ(offsetof(struct can_frame, can_id), 0);
	ATF_CHECK_EQ(offsetof(struct can_frame, can_dlc), 4);
	ATF_CHECK_EQ(offsetof(struct can_frame, __pad), 5);
	ATF_CHECK_EQ(offsetof(struct can_frame, __res0), 6);
	ATF_CHECK_EQ(offsetof(struct can_frame, __res1), 7);
	ATF_CHECK_EQ(offsetof(struct can_frame, data), 8);
	ATF_CHECK_EQ(sizeof(struct canfd_frame), 72);
	ATF_CHECK_EQ(CANFD_MTU, 72);
	ATF_CHECK_EQ(offsetof(struct canfd_frame, can_id), 0);
	ATF_CHECK_EQ(offsetof(struct canfd_frame, len), 4);
	ATF_CHECK_EQ(offsetof(struct canfd_frame, flags), 5);
	ATF_CHECK_EQ(offsetof(struct canfd_frame, __res0), 6);
	ATF_CHECK_EQ(offsetof(struct canfd_frame, __res1), 7);
	ATF_CHECK_EQ(offsetof(struct canfd_frame, data), 8);
	ATF_CHECK_EQ(sizeof(struct sockaddr_can), 16);
	ATF_CHECK_EQ(offsetof(struct sockaddr_can, can_len), 0);
	ATF_CHECK_EQ(offsetof(struct sockaddr_can, can_family), 1);
	ATF_CHECK_EQ(offsetof(struct sockaddr_can, can_ifindex), 4);
	ATF_CHECK_EQ(offsetof(struct sockaddr_can, can_addr), 8);
	ATF_CHECK_EQ(CANFD_MAX_DLEN, 64);
	ATF_CHECK_EQ(CANFD_MAX_DLC, 15);
	ATF_CHECK_EQ(CANFD_BRS, 1);
	ATF_CHECK_EQ(CANFD_ESI, 2);
	ATF_CHECK_EQ(CANFD_FDF, 4);
	ATF_CHECK_EQ(CAN_RAW_FILTER, 1);
	ATF_CHECK_EQ(CAN_RAW_LOOPBACK, 4);
	ATF_CHECK_EQ(CAN_RAW_RECV_OWN_MSGS, 5);
	ATF_CHECK_EQ(CAN_RAW_FD_FRAMES, 6);
	ATF_CHECK_EQ(CAN_LINKMODE_FD, 0x10);
}

ATF_TC(options);
ATF_TC_HEAD(options, tc)
{

	atf_tc_set_md_var(tc, "descr", "FD opt-in and invalid option lengths");
	atf_tc_set_md_var(tc, "timeout", "10");
}
ATF_TC_BODY(options, tc)
{
	int s, value = 1;
	socklen_t len;

	rump_init();
	s = new_socket(false);
	ATF_CHECK_EQ(get_option(s, CAN_RAW_FD_FRAMES), 0);
	ATF_CHECK_EQ(get_option(s, CAN_RAW_LOOPBACK), 1);
	ATF_CHECK_EQ(get_option(s, CAN_RAW_RECV_OWN_MSGS), 0);
	for (len = 0; len < sizeof(value); len++) {
		ATF_REQUIRE_ERRNO(EINVAL, rump_sys_setsockopt(s, SOL_CAN_RAW,
		    CAN_RAW_FD_FRAMES, &value, len) == -1);
		ATF_CHECK_EQ(get_option(s, CAN_RAW_FD_FRAMES), 0);
	}
	set_option(s, CAN_RAW_FD_FRAMES, 1);
	ATF_CHECK_EQ(get_option(s, CAN_RAW_FD_FRAMES), 1);
	value = 0;
	ATF_REQUIRE_ERRNO(EINVAL, rump_sys_setsockopt(s, SOL_CAN_RAW,
	    CAN_RAW_FD_FRAMES, &value, sizeof(value) - 1) == -1);
	ATF_CHECK_EQ(get_option(s, CAN_RAW_FD_FRAMES), 1);
	set_option(s, CAN_RAW_FD_FRAMES, 0);
	ATF_CHECK_EQ(get_option(s, CAN_RAW_FD_FRAMES), 0);
	close_socket(s);
	s = new_socket(false);
	ATF_CHECK_EQ(get_option(s, CAN_RAW_FD_FRAMES), 0);
	close_socket(s);
}

ATF_TC(lengths);
ATF_TC_HEAD(lengths, tc)
{

	atf_tc_set_md_var(tc, "descr", "all FD payload lengths and BRS/ESI flags");
	atf_tc_set_md_var(tc, "timeout", "20");
}
ATF_TC_BODY(lengths, tc)
{
	struct canfd_frame frame;
	unsigned int len;
	int tx, rx, index;

	rump_init();
	tx = new_socket(true);
	rx = new_socket(true);
	index = create_fd_if(tx, "canlo0");
	bind_socket(tx, index);
	bind_socket(rx, index);
	for (len = 0; len <= CANFD_MAX_DLEN; len++) {
		make_fd(&frame, len, len % 8);
		if ((len & 1) != 0)
			frame.can_id = CAN_EFF_FLAG | 0x1234567;
		send_frame(tx, &frame, sizeof(frame));
		receive_fd(rx, &frame, index);
	}
	expect_none(tx);
	close_socket(rx);
	close_socket(tx);
}

ATF_TC(invalid_frames);
ATF_TC_HEAD(invalid_frames, tc)
{

	atf_tc_set_md_var(tc, "descr", "reject malformed FD and classic records");
	atf_tc_set_md_var(tc, "timeout", "20");
}
ATF_TC_BODY(invalid_frames, tc)
{
	struct canfd_frame frame;
	struct can_frame classic;
	uint8_t oversized[CANFD_MTU + 1];
	unsigned int len;
	int tx, rx, index;

	rump_init();
	tx = new_socket(true);
	rx = new_socket(true);
	index = create_fd_if(tx, "canlo0");
	bind_socket(tx, index);
	bind_socket(rx, index);
	make_fd(&frame, 65, 0);
	reject_frame(tx, &frame, sizeof(frame), EINVAL);
	frame.len = 255;
	reject_frame(tx, &frame, sizeof(frame), EINVAL);
	make_fd(&frame, 64, 8);
	reject_frame(tx, &frame, sizeof(frame), EINVAL);
	frame.flags = 0;
	frame.can_id |= CAN_RTR_FLAG;
	reject_frame(tx, &frame, sizeof(frame), EINVAL);
	frame.can_id = CAN_ERR_FLAG | 0x123;
	reject_frame(tx, &frame, sizeof(frame), EINVAL);
	make_fd(&frame, 8, 0);
	for (len = CAN_MTU + 1; len < CANFD_MTU; len++)
		reject_frame(tx, &frame, len, EINVAL);
	memset(oversized, 0, sizeof(oversized));
	memcpy(oversized, &frame, sizeof(frame));
	reject_frame(tx, oversized, sizeof(oversized), EINVAL);
	memset(&classic, 0, sizeof(classic));
	classic.can_id = 0x123;
	for (len = 1; len < offsetof(struct can_frame, data); len++)
		reject_frame(tx, &classic, len, EINVAL);
	for (len = 1; len <= CAN_MAX_DLEN; len++) {
		classic.can_dlc = len;
		reject_frame(tx, &classic,
		    offsetof(struct can_frame, data) + len - 1, EINVAL);
	}
	classic.can_dlc = 9;
	reject_frame(tx, &classic, sizeof(classic), EINVAL);
	classic.can_dlc = 255;
	reject_frame(tx, &classic, sizeof(classic), EINVAL);
	expect_none(rx);
	make_fd(&frame, 64, CANFD_BRS | CANFD_ESI);
	send_frame(tx, &frame, sizeof(frame));
	receive_fd(rx, &frame, index);
	close_socket(rx);
	close_socket(tx);
}

ATF_TC(mixed);
ATF_TC_HEAD(mixed, tc)
{

	atf_tc_set_md_var(tc, "descr", "classic isolation and short classic ABI");
	atf_tc_set_md_var(tc, "timeout", "20");
}
ATF_TC_BODY(mixed, tc)
{
	struct canfd_frame frame;
	struct can_frame classic;
	unsigned int len;
	int tx, fd, old, index;
	size_t size;

	rump_init();
	tx = new_socket(true);
	fd = new_socket(true);
	old = new_socket(false);
	index = create_fd_if(tx, "canlo0");
	bind_socket(tx, index);
	bind_socket(fd, index);
	bind_socket(old, index);
	make_fd(&frame, 64, CANFD_BRS);
	reject_frame(old, &frame, sizeof(frame), EINVAL);
	send_frame(tx, &frame, sizeof(frame));
	receive_fd(fd, &frame, index);
	expect_none(old);
	memset(&classic, 0, sizeof(classic));
	classic.can_id = 0x456;
	memset(classic.data, 0x5a, sizeof(classic.data));
	for (len = 0; len <= CAN_MAX_DLEN; len++) {
		classic.can_dlc = len;
		size = offsetof(struct can_frame, data) + len;
		send_frame(tx, &classic, size);
		receive_frame(old, &classic, size, index);
		receive_frame(fd, &classic, size, index);
	}
	set_option(fd, CAN_RAW_FD_FRAMES, 0);
	send_frame(tx, &frame, sizeof(frame));
	expect_none(fd);
	expect_none(old);
	set_option(fd, CAN_RAW_FD_FRAMES, 1);
	send_frame(tx, &frame, sizeof(frame));
	receive_fd(fd, &frame, index);
	send_frame(old, &classic, sizeof(classic));
	receive_frame(fd, &classic, sizeof(classic), index);
	receive_frame(tx, &classic, sizeof(classic), index);
	close_socket(old);
	close_socket(fd);
	close_socket(tx);
}

ATF_TC(filters);
ATF_TC_HEAD(filters, tc)
{

	atf_tc_set_md_var(tc, "descr", "FD uses CAN identifier filters");
	atf_tc_set_md_var(tc, "timeout", "15");
}
ATF_TC_BODY(filters, tc)
{
	struct canfd_frame frame;
	struct can_filter filter;
	int tx, rx, index;

	rump_init();
	tx = new_socket(true);
	rx = new_socket(true);
	index = create_fd_if(tx, "canlo0");
	bind_socket(tx, index);
	bind_socket(rx, index);
	filter.can_id = 0x123;
	filter.can_mask = CAN_SFF_MASK | CAN_EFF_FLAG;
	ATF_REQUIRE_EQ(rump_sys_setsockopt(rx, SOL_CAN_RAW, CAN_RAW_FILTER,
	    &filter, sizeof(filter)), 0);
	make_fd(&frame, 64, CANFD_BRS);
	send_frame(tx, &frame, sizeof(frame));
	receive_fd(rx, &frame, index);
	frame.can_id++;
	send_frame(tx, &frame, sizeof(frame));
	expect_none(rx);
	frame.can_id = CAN_EFF_FLAG | 0x123;
	send_frame(tx, &frame, sizeof(frame));
	expect_none(rx);
	filter.can_id = frame.can_id;
	filter.can_mask = CAN_EFF_FLAG | CAN_EFF_MASK;
	ATF_REQUIRE_EQ(rump_sys_setsockopt(rx, SOL_CAN_RAW, CAN_RAW_FILTER,
	    &filter, sizeof(filter)), 0);
	send_frame(tx, &frame, sizeof(frame));
	receive_fd(rx, &frame, index);
	ATF_REQUIRE_EQ(rump_sys_setsockopt(rx, SOL_CAN_RAW, CAN_RAW_FILTER,
	    NULL, 0), 0);
	send_frame(tx, &frame, sizeof(frame));
	expect_none(rx);
	close_socket(rx);
	close_socket(tx);
}

ATF_TC(loopback);
ATF_TC_HEAD(loopback, tc)
{

	atf_tc_set_md_var(tc, "descr", "FD loopback and own-message controls");
	atf_tc_set_md_var(tc, "timeout", "15");
}
ATF_TC_BODY(loopback, tc)
{
	struct canfd_frame frame;
	int tx, rx, index;

	rump_init();
	tx = new_socket(true);
	rx = new_socket(true);
	index = create_fd_if(tx, "canlo0");
	bind_socket(tx, index);
	bind_socket(rx, index);
	make_fd(&frame, 64, CANFD_ESI);
	send_frame(tx, &frame, sizeof(frame));
	receive_fd(rx, &frame, index);
	expect_none(tx);
	set_option(tx, CAN_RAW_RECV_OWN_MSGS, 1);
	send_frame(tx, &frame, sizeof(frame));
	receive_fd(tx, &frame, index);
	receive_fd(rx, &frame, index);
	set_option(tx, CAN_RAW_LOOPBACK, 0);
	ATF_CHECK_EQ(get_option(tx, CAN_RAW_LOOPBACK), 0);
	send_frame(tx, &frame, sizeof(frame));
	expect_none(tx);
	expect_none(rx);
	set_option(tx, CAN_RAW_LOOPBACK, 1);
	send_frame(tx, &frame, sizeof(frame));
	receive_fd(tx, &frame, index);
	receive_fd(rx, &frame, index);
	set_option(tx, CAN_RAW_RECV_OWN_MSGS, 0);
	send_frame(tx, &frame, sizeof(frame));
	receive_fd(rx, &frame, index);
	expect_none(tx);
	close_socket(rx);
	close_socket(tx);
}

ATF_TC(wildcard);
ATF_TC_HEAD(wildcard, tc)
{

	atf_tc_set_md_var(tc, "descr", "FD wildcard source and interface isolation");
	atf_tc_set_md_var(tc, "timeout", "15");
}
ATF_TC_BODY(wildcard, tc)
{
	struct sockaddr_can sa;
	struct canfd_frame frame;
	int tx, rx, bound, first, second;

	rump_init();
	tx = new_socket(true);
	rx = new_socket(true);
	bound = new_socket(true);
	first = create_fd_if(tx, "canlo0");
	second = create_fd_if(tx, "canlo1");
	bind_socket(tx, 0);
	bind_socket(rx, 0);
	bind_socket(bound, first);
	memset(&sa, 0, sizeof(sa));
	sa.can_len = sizeof(sa);
	sa.can_family = AF_CAN;
	sa.can_ifindex = first;
	make_fd(&frame, 64, CANFD_BRS);
	ATF_REQUIRE_EQ(rump_sys_sendto(tx, &frame, sizeof(frame), 0,
	    (struct sockaddr *)&sa, sizeof(sa)), sizeof(frame));
	receive_fd(rx, &frame, first);
	receive_fd(bound, &frame, first);
	sa.can_ifindex = second;
	frame.can_id++;
	ATF_REQUIRE_EQ(rump_sys_sendto(tx, &frame, sizeof(frame), 0,
	    (struct sockaddr *)&sa, sizeof(sa)), sizeof(frame));
	receive_fd(rx, &frame, second);
	expect_none(bound);
	close_socket(bound);
	close_socket(rx);
	close_socket(tx);
}

ATF_TC(interface_modes);
ATF_TC_HEAD(interface_modes, tc)
{

	atf_tc_set_md_var(tc, "descr", "CAN loopback FD mode and MTU consistency");
	atf_tc_set_md_var(tc, "timeout", "15");
}
ATF_TC_BODY(interface_modes, tc)
{
	struct can_link_timecaps caps;
	uint32_t mode;
	int s;

	rump_init();
	s = new_socket(false);
	create_if(s, "canlo0");
	memset(&caps, 0, sizeof(caps));
	ATF_REQUIRE_EQ(link_ioctl(s, "canlo0", SIOCGDRVSPEC,
	    CANGLINKTIMECAP, &caps, sizeof(caps)), 0);
	ATF_CHECK((caps.cltc_linkmode_caps & CAN_LINKMODE_FD) != 0);
	ATF_CHECK_EQ(caps.cltc_clock_freq, 0);
	ATF_CHECK_EQ(get_mtu(s, "canlo0"), CAN_MTU);
	ATF_CHECK_EQ(get_mode(s, "canlo0") & CAN_LINKMODE_FD, 0);
	set_fd_mode(s, "canlo0", true);
	ATF_CHECK_EQ(get_mtu(s, "canlo0"), CANFD_MTU);
	ATF_CHECK_EQ(get_mode(s, "canlo0") & CAN_LINKMODE_FD,
	    CAN_LINKMODE_FD);
	set_up(s, "canlo0", true);
	mode = CAN_LINKMODE_FD;
	ATF_REQUIRE_ERRNO(EBUSY, link_ioctl(s, "canlo0", SIOCSDRVSPEC,
	    CANCLINKMODE, &mode, sizeof(mode)) == -1);
	ATF_REQUIRE_ERRNO(EBUSY, set_mtu(s, "canlo0", CAN_MTU) == -1);
	ATF_CHECK_EQ(get_mtu(s, "canlo0"), CANFD_MTU);
	ATF_CHECK_EQ(get_mode(s, "canlo0") & CAN_LINKMODE_FD,
	    CAN_LINKMODE_FD);
	set_up(s, "canlo0", false);
	set_fd_mode(s, "canlo0", false);
	ATF_CHECK_EQ(get_mtu(s, "canlo0"), CAN_MTU);
	ATF_CHECK_EQ(get_mode(s, "canlo0") & CAN_LINKMODE_FD, 0);
	ATF_REQUIRE_EQ(set_mtu(s, "canlo0", CANFD_MTU), 0);
	ATF_CHECK_EQ(get_mode(s, "canlo0") & CAN_LINKMODE_FD,
	    CAN_LINKMODE_FD);
	ATF_REQUIRE_ERRNO(EINVAL, set_mtu(s, "canlo0", 17) == -1);
	ATF_CHECK_EQ(get_mtu(s, "canlo0"), CANFD_MTU);
	ATF_REQUIRE_EQ(set_mtu(s, "canlo0", CAN_MTU), 0);
	ATF_CHECK_EQ(get_mode(s, "canlo0") & CAN_LINKMODE_FD, 0);
	mode = 0x80000000U;
	ATF_REQUIRE_ERRNO(EINVAL, link_ioctl(s, "canlo0", SIOCSDRVSPEC,
	    CANSLINKMODE, &mode, sizeof(mode)) == -1);
	mode = CAN_LINKMODE_FD;
	ATF_REQUIRE_ERRNO(EINVAL, link_ioctl(s, "canlo0", SIOCSDRVSPEC,
	    CANSLINKMODE, &mode, sizeof(mode) - 1) == -1);
	destroy_if(s, "canlo0");
	close_socket(s);
}

ATF_TC(interface_rejection);
ATF_TC_HEAD(interface_rejection, tc)
{

	atf_tc_set_md_var(tc, "descr", "reject FD on classic MTU or down links");
	atf_tc_set_md_var(tc, "timeout", "15");
}
ATF_TC_BODY(interface_rejection, tc)
{
	struct canfd_frame frame;
	struct can_frame classic;
	int tx, rx, index;

	rump_init();
	tx = new_socket(true);
	rx = new_socket(true);
	index = create_if(tx, "canlo0");
	bind_socket(tx, index);
	bind_socket(rx, index);
	set_up(tx, "canlo0", true);
	make_fd(&frame, 64, 0);
	reject_frame(tx, &frame, sizeof(frame), EMSGSIZE);
	expect_none(rx);
	set_up(tx, "canlo0", false);
	set_fd_mode(tx, "canlo0", true);
	reject_frame(tx, &frame, sizeof(frame), ENETDOWN);
	expect_none(rx);
	set_up(tx, "canlo0", true);
	send_frame(tx, &frame, sizeof(frame));
	receive_fd(rx, &frame, index);
	set_up(tx, "canlo0", false);
	set_fd_mode(tx, "canlo0", false);
	set_up(tx, "canlo0", true);
	reject_frame(tx, &frame, sizeof(frame), EMSGSIZE);
	memset(&classic, 0, sizeof(classic));
	classic.can_id = 0x123;
	classic.can_dlc = CAN_MAX_DLEN;
	memset(classic.data, 0x96, sizeof(classic.data));
	send_frame(tx, &classic, sizeof(classic));
	receive_frame(rx, &classic, sizeof(classic), index);
	close_socket(rx);
	close_socket(tx);
}

ATF_TC(lifetime);
ATF_TC_HEAD(lifetime, tc)
{

	atf_tc_set_md_var(tc, "descr", "repeated FD rejection, close and destroy");
	atf_tc_set_md_var(tc, "timeout", "30");
}
ATF_TC_BODY(lifetime, tc)
{
	struct canfd_frame frame;
	int control, tx, rx, index, round, packet;

	rump_init();
	control = new_socket(false);
	make_fd(&frame, 64, CANFD_BRS | CANFD_ESI);
	for (round = 0; round < 32; round++) {
		tx = new_socket(true);
		rx = new_socket(true);
		index = create_if(control, "canlo0");
		bind_socket(tx, index);
		bind_socket(rx, index);
		set_up(control, "canlo0", true);
		for (packet = 0; packet < 16; packet++)
			reject_frame(tx, &frame, sizeof(frame), EMSGSIZE);
		set_up(control, "canlo0", false);
		set_fd_mode(control, "canlo0", true);
		for (packet = 0; packet < 16; packet++)
			reject_frame(tx, &frame, sizeof(frame), ENETDOWN);
		set_up(control, "canlo0", true);
		send_frame(tx, &frame, sizeof(frame));
		receive_fd(rx, &frame, index);
		/* Leave records queued while both socket PCBs are detached. */
		set_option(tx, CAN_RAW_RECV_OWN_MSGS, 1);
		for (packet = 0; packet < 16; packet++)
			send_frame(tx, &frame, sizeof(frame));
		close_socket(tx);
		close_socket(rx);
		destroy_if(control, "canlo0");
	}
	close_socket(control);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, abi);
	ATF_TP_ADD_TC(tp, options);
	ATF_TP_ADD_TC(tp, lengths);
	ATF_TP_ADD_TC(tp, invalid_frames);
	ATF_TP_ADD_TC(tp, mixed);
	ATF_TP_ADD_TC(tp, filters);
	ATF_TP_ADD_TC(tp, loopback);
	ATF_TP_ADD_TC(tp, wildcard);
	ATF_TP_ADD_TC(tp, interface_modes);
	ATF_TP_ADD_TC(tp, interface_rejection);
	ATF_TP_ADD_TC(tp, lifetime);
	return atf_no_error();
}
