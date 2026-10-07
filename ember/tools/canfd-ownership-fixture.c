/* Origin: EmberBSD; AI-assisted CAN mbuf fault-contract plumbing. */
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

#include <sys/types.h>
#include <sys/socket.h>

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __aligned
#define __aligned(n) __attribute__((__aligned__(n)))
#endif
#include "can-layout.h"
#include "can-link-layout.h"
#include "can-pcb-flags.h"

/* Kernel API seams only; CAN functions are in production.h below. */
#define M_PKTHDR		0x01
#define PACKET_TAG_SO		1
#define PR_NOWAIT		0
#define IFF_UP			0x01
#define IFF_RUNNING		0x40
#define SS_ISCONNECTED		0x02
#ifndef AF_CAN
#define AF_CAN			29
#endif
#define KASSERT(condition)	assert(condition)
#define mtod(m, type)		((type)(m)->data)
#define sotocanpcb(so)		((so)->so_pcb)
#define if_statinc(ifp, field)	((ifp)->field++)
#define STUB			static __attribute__((unused))
#define CHECK(condition)	check((condition), #condition, __LINE__)

struct m_tag {
	uintptr_t type;
};

struct mbuf {
	int m_len;
	int m_flags;
	struct {
		size_t len;
	} m_pkthdr;
	uint8_t data[72] __aligned(8);
	struct m_tag *tag;
	unsigned int free_calls;
	unsigned int frees;
};

struct canif_softc {
	uint32_t csc_linkmodes;
};

struct ifnet {
	int if_flags;
	size_t if_mtu;
	struct canif_softc *if_softc;
	unsigned int if_oerrors;
};

struct canpcb {
	struct ifnet *canp_ifp;
	int canp_mtx;
	int canp_flags;
	int refs;
};

struct socket {
	struct canpcb *so_pcb;
	int so_state;
};

struct lwp {
	int unused;
};

/* A tag's payload immediately follows its header, as in the kernel API. */
static struct {
	struct m_tag tag;
	struct canpcb *sender;
} tag_storage;
static struct mbuf packet;
static struct canif_softc controller;
static struct ifnet iface;
static struct canpcb pcb;
static struct socket sock;
static const char *case_name;
static unsigned int failures, cases;
static unsigned int tag_gets, tag_frees, tag_finds, refs, unrefs;
static unsigned int enqueues, pullups, binds;
static bool tag_live, fail_tag, fail_pullup;
static int queue_error, bind_error, spl_depth;
static int can_output_cnt;

static void
check(bool condition, const char *expression, int line)
{

	if (condition)
		return;
	fprintf(stderr, "FAIL %s:%d: %s\n", case_name, line, expression);
	failures++;
}

STUB void
mutex_enter(int *lock)
{

	CHECK(*lock == 0);
	*lock = 1;
}

STUB void
mutex_exit(int *lock)
{

	CHECK(*lock == 1);
	*lock = 0;
}

STUB void
canp_ref(struct canpcb *sender)
{

	CHECK(sender->canp_mtx == 1);
	sender->refs++;
	refs++;
}

STUB void
canp_unref(struct canpcb *sender)
{

	CHECK(sender->refs > 1);
	sender->refs--;
	unrefs++;
}

STUB struct m_tag *
m_tag_get(int type, size_t len, int how)
{

	tag_gets++;
	CHECK(type == PACKET_TAG_SO);
	CHECK(len == sizeof(struct canpcb *));
	CHECK(how == PR_NOWAIT);
	if (fail_tag)
		return NULL;
	CHECK(!tag_live);
	tag_live = true;
	tag_storage.tag.type = type;
	tag_storage.sender = NULL;
	return &tag_storage.tag;
}

STUB void
m_tag_prepend(struct mbuf *m, struct m_tag *tag)
{

	CHECK(m->frees == 0);
	CHECK((m->m_flags & M_PKTHDR) != 0);
	CHECK(m->tag == NULL);
	m->tag = tag;
}

STUB struct m_tag *
m_tag_find(struct mbuf *m, int type)
{

	tag_finds++;
	CHECK(m->frees == 0);
	CHECK((m->m_flags & M_PKTHDR) != 0);
	CHECK(type == PACKET_TAG_SO);
	return m->tag;
}

STUB void
m_freem(struct mbuf *m)
{

	if (m == NULL)
		return;
	m->free_calls++;
	/* Retain the ledger to detect a second free without invoking UB. */
	CHECK(m->frees == 0);
	if (m->frees != 0)
		return;
	m->frees++;
	if (m->tag != NULL) {
		CHECK(tag_live);
		tag_live = false;
		tag_frees++;
		m->tag = NULL;
	}
	/* m_freem releases tags, not the sender reference inside the tag. */
}

STUB struct mbuf *
m_pullup(struct mbuf *m, int len)
{

	pullups++;
	CHECK(m->frees == 0);
	if (fail_pullup) {
		m_freem(m);
		return NULL;
	}
	m->m_len = len;
	return m;
}

STUB int
ifq_enqueue(struct ifnet *ifp, struct mbuf *m)
{

	enqueues++;
	CHECK(ifp == &iface);
	CHECK(m->frees == 0);
	CHECK(m->tag != NULL);
	CHECK(tag_storage.sender == &pcb);
	CHECK(pcb.refs == 2);
	/* The real ifq_enqueue consumes the mbuf even when it fails. */
	if (queue_error != 0)
		m_freem(m);
	return queue_error;
}

STUB int
splnet(void)
{

	return spl_depth++;
}

STUB void
splx(int previous)
{

	CHECK(spl_depth == previous + 1);
	spl_depth = previous;
}

STUB int
can_pcbbind(struct canpcb *sender, struct sockaddr_can *sa, struct lwp *l)
{

	binds++;
	CHECK(spl_depth == 1);
	if (bind_error != 0)
		return bind_error;
	sender->canp_ifp = sa->can_ifindex != 0 ? &iface : NULL;
	return 0;
}

#include "production.h"

static void
start_case(const char *name)
{
	struct can_frame *frame;

	case_name = name;
	cases++;
	memset(&packet, 0, sizeof(packet));
	memset(&controller, 0, sizeof(controller));
	memset(&iface, 0, sizeof(iface));
	memset(&pcb, 0, sizeof(pcb));
	memset(&sock, 0, sizeof(sock));
	memset(&tag_storage, 0, sizeof(tag_storage));
	tag_gets = tag_frees = tag_finds = refs = unrefs = 0;
	enqueues = pullups = binds = 0;
	tag_live = fail_tag = fail_pullup = false;
	queue_error = bind_error = spl_depth = can_output_cnt = 0;
	packet.m_len = CAN_MTU;
	packet.m_flags = M_PKTHDR;
	packet.m_pkthdr.len = CAN_MTU;
	frame = mtod(&packet, struct can_frame *);
	frame->can_id = 0x123;
	frame->can_dlc = CAN_MAX_DLEN;
	iface.if_flags = IFF_UP | IFF_RUNNING;
	iface.if_mtu = CAN_MTU;
	iface.if_softc = &controller;
	pcb.canp_ifp = &iface;
	pcb.refs = 1;
#ifdef CANP_FD_FRAMES
	pcb.canp_flags = CANP_FD_FRAMES;
#endif
	sock.so_pcb = &pcb;
	sock.so_state = SS_ISCONNECTED;
}

static void
check_freed(void)
{

	CHECK(packet.free_calls == 1);
	CHECK(packet.frees == 1);
	CHECK(!tag_live);
	CHECK(pcb.refs == 1);
	CHECK(pcb.canp_mtx == 0);
}

static void
check_early_rejection(void)
{

	check_freed();
	CHECK(tag_gets == 0);
	CHECK(refs == 0);
	CHECK(unrefs == 0);
	CHECK(enqueues == 0);
}

static void
check_send_failures(void)
{
	struct sockaddr_can sa;

	start_case("queue ENOBUFS through can_send");
	queue_error = ENOBUFS;
	CHECK(can_send(&sock, &packet, NULL, NULL, NULL) == ENOBUFS);
	check_freed();
	CHECK(enqueues == 1);
	CHECK(tag_gets == 1);
	CHECK(tag_frees == 1);
	CHECK(refs == 1);
	CHECK(unrefs == 1);

	start_case("tag allocation failure through can_send");
	fail_tag = true;
	CHECK(can_send(&sock, &packet, NULL, NULL, NULL) == ENOMEM);
	check_freed();
	CHECK(tag_gets == 1);
	CHECK(tag_frees == 0);
	CHECK(refs == 0);
	CHECK(unrefs == 0);
	CHECK(enqueues == 0);
	CHECK(iface.if_oerrors == 1);

	start_case("interface down before sender reference");
	iface.if_flags &= ~IFF_UP;
	CHECK(can_send(&sock, &packet, NULL, NULL, NULL) == ENETDOWN);
	check_early_rejection();

	start_case("interface not running before sender reference");
	iface.if_flags &= ~IFF_RUNNING;
	CHECK(can_send(&sock, &packet, NULL, NULL, NULL) == ENETDOWN);
	check_early_rejection();

	start_case("MTU rejection before sender reference");
	packet.m_len = packet.m_pkthdr.len = sizeof(packet.data);
	CHECK(can_output(&packet, &pcb) == EMSGSIZE);
	check_early_rejection();

	start_case("missing interface consumes packet");
	pcb.canp_ifp = NULL;
	CHECK(can_output(&packet, &pcb) == EDESTADDRREQ);
	check_early_rejection();

	start_case("missing PCB consumes packet");
	CHECK(can_output(&packet, NULL) == EINVAL);
	check_early_rejection();

	start_case("listen-only rejection before sender reference");
	controller.csc_linkmodes = CAN_LINKMODE_LISTENONLY;
	CHECK(can_send(&sock, &packet, NULL, NULL, NULL) == ENETUNREACH);
	check_early_rejection();

	start_case("sendto queue failure restores interrupt level");
	memset(&sa, 0, sizeof(sa));
	sa.can_len = sizeof(sa);
	sa.can_family = AF_CAN;
	sa.can_ifindex = 1;
	sock.so_state = 0;
	queue_error = ENOBUFS;
	CHECK(can_send(&sock, &packet, (struct sockaddr *)&sa,
	    NULL, NULL) == ENOBUFS);
	check_freed();
	CHECK(spl_depth == 0);
	CHECK(binds == 2);
	CHECK(pcb.canp_ifp == NULL);
	CHECK(refs == 1);
	CHECK(unrefs == 1);

	start_case("sendto bind failure restores interrupt level");
	sock.so_state = 0;
	bind_error = ENXIO;
	CHECK(can_send(&sock, &packet, (struct sockaddr *)&sa,
	    NULL, NULL) == ENXIO);
	check_early_rejection();
	CHECK(spl_depth == 0);
	CHECK(binds == 1);
}

#ifdef CAN_OWNERSHIP_HELPERS
static void
add_sender_tag(void)
{
	struct m_tag *tag;

	tag = m_tag_get(PACKET_TAG_SO, sizeof(struct canpcb *), PR_NOWAIT);
	assert(tag != NULL);
	mutex_enter(&pcb.canp_mtx);
	canp_ref(&pcb);
	mutex_exit(&pcb.canp_mtx);
	*(struct canpcb **)(tag + 1) = &pcb;
	m_tag_prepend(&packet, tag);
}

static void
check_helpers(void)
{

	start_case("successful send holds reference until packet disposal");
	CHECK(can_send(&sock, &packet, NULL, NULL, NULL) == 0);
	CHECK(enqueues == 1);
	CHECK(packet.frees == 0);
	CHECK(pcb.refs == 2);
	CHECK(unrefs == 0);
	can_mbuf_free(&packet);
	check_freed();
	CHECK(refs == 1);
	CHECK(unrefs == 1);
	CHECK(tag_frees == 1);

	start_case("tagged packet disposal releases sender");
	add_sender_tag();
	can_mbuf_free(&packet);
	check_freed();
	CHECK(tag_finds == 1);
	CHECK(tag_frees == 1);
	CHECK(unrefs == 1);

	start_case("headerless disposal never inspects packet tags");
	packet.m_flags = 0;
	memset(packet.data, 0xa5, sizeof(packet.data));
	can_mbuf_free(&packet);
	check_freed();
	CHECK(tag_finds == 0);
	CHECK(refs == 0);
	CHECK(unrefs == 0);

	start_case("NULL disposal does nothing");
	can_mbuf_free(NULL);
	CHECK(packet.free_calls == 0);
	CHECK(tag_finds == 0);
	CHECK(unrefs == 0);

	start_case("failed pullup releases consumed tagged sender");
	add_sender_tag();
	packet.m_len = 4;
	fail_pullup = true;
	CHECK(can_mbuf_pullup(&packet, CAN_MTU) == NULL);
	check_freed();
	CHECK(pullups == 1);
	CHECK(tag_frees == 1);
	CHECK(unrefs == 1);

	start_case("failed untagged pullup through can_send frees once");
	packet.m_len = 4;
	fail_pullup = true;
	CHECK(can_send(&sock, &packet, NULL, NULL, NULL) == ENOBUFS);
	check_early_rejection();
	CHECK(pullups == 1);

	start_case("successful pullup retains sender until disposal");
	add_sender_tag();
	packet.m_len = 4;
	CHECK(can_mbuf_pullup(&packet, CAN_MTU) == &packet);
	CHECK(pullups == 1);
	CHECK(packet.frees == 0);
	CHECK(pcb.refs == 2);
	CHECK(unrefs == 0);
	can_mbuf_free(&packet);
	check_freed();
	CHECK(unrefs == 1);

	start_case("contiguous packet avoids pullup and keeps sender");
	add_sender_tag();
	CHECK(can_mbuf_pullup(&packet, CAN_MTU) == &packet);
	CHECK(pullups == 0);
	CHECK(tag_finds == 0);
	CHECK(pcb.refs == 2);
	can_mbuf_free(&packet);
	check_freed();
	CHECK(unrefs == 1);
}
#endif

int
main(void)
{

	check_send_failures();
#ifdef CAN_OWNERSHIP_HELPERS
	check_helpers();
#else
	puts("Source has no CAN mbuf helpers; helper-only cases unavailable.");
#endif
	printf("%s: %u CAN ownership cases, %u failures\n",
	    failures == 0 ? "PASS" : "FAIL", cases, failures);
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
