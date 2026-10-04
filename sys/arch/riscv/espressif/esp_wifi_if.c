/*-
 * Copyright (c) 2026 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The network interface of the ESP32-S31 radio.
 *
 * The vendor's libraries keep the whole of 802.11 to themselves and trade
 * Ethernet frames, so the interface is an Ethernet one.  Frames go out from
 * a thread of the radio code, because sending may sleep there; frames come
 * in on the thread of the libraries.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/if_ether.h>
#include <net/if_media.h>
#include <net/bpf.h>

#include <riscv/espressif/esp_wifi_os.h>
#include <riscv/espressif/esp_wifi_var.h>

#define	ESPWIFI_MTU_FRAME	1600

struct espwifi_softc {
	struct ethercom	sc_ec;
	struct ifmedia	sc_media;
	struct if_percpuq *sc_ipq;
	void		*sc_txsem;
	uint8_t		*sc_txbuf;
	bool		sc_attached;
	bool		sc_link;
};

static struct espwifi_softc espwifi_sc;

static void
espwifi_if_start(struct ifnet *ifp)
{
	struct espwifi_softc * const sc = ifp->if_softc;

	espwifi_os_sem_give(sc->sc_txsem);
}

static void
espwifi_if_txthread(void *v)
{
	struct espwifi_softc * const sc = v;
	struct ifnet * const ifp = &sc->sc_ec.ec_if;

	for (;;) {
		espwifi_os_sem_take(sc->sc_txsem, ESPWIFI_WAIT_FOREVER);
		for (;;) {
			struct mbuf *m;
			const int s = splnet();

			IFQ_DEQUEUE(&ifp->if_snd, m);
			splx(s);
			if (m == NULL)
				break;

			const u_int len = m->m_pkthdr.len;
			if (len > ESPWIFI_MTU_FRAME || !sc->sc_link) {
				if_statinc(ifp, if_oerrors);
				m_freem(m);
				continue;
			}
			m_copydata(m, 0, len, sc->sc_txbuf);
			bpf_mtap(ifp, m, BPF_D_OUT);
			m_freem(m);
			if (espwifi_tx(sc->sc_txbuf, len) == 0)
				if_statinc(ifp, if_opackets);
			else
				if_statinc(ifp, if_oerrors);
		}
	}
}

void
espwifi_if_input(const void *buf, unsigned int len)
{
	struct espwifi_softc * const sc = &espwifi_sc;
	struct ifnet * const ifp = &sc->sc_ec.ec_if;

	if (!sc->sc_attached || (ifp->if_flags & IFF_RUNNING) == 0)
		return;

	struct mbuf * const m = m_devget(__UNCONST(buf), len, 0, ifp);
	if (m == NULL) {
		if_statinc(ifp, if_ierrors);
		return;
	}
	const int s = splnet();
	if_percpuq_enqueue(sc->sc_ipq, m);
	splx(s);
}

void
espwifi_if_link(int up)
{
	struct espwifi_softc * const sc = &espwifi_sc;

	sc->sc_link = up != 0;
	if (sc->sc_attached)
		if_link_state_change(&sc->sc_ec.ec_if,
		    up ? LINK_STATE_UP : LINK_STATE_DOWN);
}

static int
espwifi_if_init(struct ifnet *ifp)
{
	ifp->if_flags |= IFF_RUNNING;
	return 0;
}

static void
espwifi_if_stop(struct ifnet *ifp, int disable)
{
	ifp->if_flags &= ~IFF_RUNNING;
}

static int
espwifi_if_ioctl(struct ifnet *ifp, u_long cmd, void *data)
{
	const int s = splnet();
	int error = ether_ioctl(ifp, cmd, data);

	if (error == ENETRESET)
		error = 0;
	splx(s);
	return error;
}

static int
espwifi_if_mediachange(struct ifnet *ifp)
{
	return 0;
}

static void
espwifi_if_mediastatus(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct espwifi_softc * const sc = ifp->if_softc;

	imr->ifm_status = IFM_AVALID | (sc->sc_link ? IFM_ACTIVE : 0);
	imr->ifm_active = IFM_ETHER | IFM_AUTO;
}

void
espwifi_if_attach(void)
{
	struct espwifi_softc * const sc = &espwifi_sc;
	struct ifnet * const ifp = &sc->sc_ec.ec_if;
	uint8_t enaddr[ETHER_ADDR_LEN];
	void *handle;

	espwifi_get_mac(enaddr);
	sc->sc_txbuf = malloc(ESPWIFI_MTU_FRAME, M_DEVBUF, M_WAITOK);
	sc->sc_txsem = espwifi_os_sem_create(1, 0);

	strlcpy(ifp->if_xname, "espwifi0", IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = espwifi_if_ioctl;
	ifp->if_start = espwifi_if_start;
	ifp->if_init = espwifi_if_init;
	ifp->if_stop = espwifi_if_stop;
	IFQ_SET_READY(&ifp->if_snd);

	sc->sc_ec.ec_ifmedia = &sc->sc_media;
	ifmedia_init(&sc->sc_media, 0, espwifi_if_mediachange,
	    espwifi_if_mediastatus);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);

	if_initialize(ifp);
	sc->sc_ipq = if_percpuq_create(ifp);
	if_deferred_start_init(ifp, NULL);
	ether_ifattach(ifp, enaddr);
	if_register(ifp);
	if_link_state_change(ifp, LINK_STATE_DOWN);
	sc->sc_attached = true;

	printf("espwifi0: Ethernet address %s\n", ether_sprintf(enaddr));
	if (!espwifi_os_task_create(espwifi_if_txthread, "tx", 0, sc,
	    &handle))
		printf("espwifi0: cannot create the transmit thread\n");
}
