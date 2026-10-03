/*-
 * Copyright (c) 2026 Anton and oxtorg contributors
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


/*
 * Allwinner A733 crypto engine, as a source of random numbers.
 *
 * The engine takes a task descriptor in memory and writes the result to
 * the address the descriptor names. Only the true random number generator
 * is used here: each task returns 32 bytes. The descriptor format differs
 * from the one sun8icrypto(4) drives: addresses are 40 bits wide and
 * stored unaligned.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/mutex.h>
#include <sys/rndsource.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>

#define	CE_TSK0_REG		0x00	/* task descriptor address, low */
#define	CE_TSK1_REG		0x04	/* task descriptor address, high */
#define	CE_ICR_REG		0x08
#define	 CE_ICR_CHAN0		__BIT(0)
#define	CE_ISR_REG		0x0c
#define	 CE_ISR_CHAN0		__BITS(1,0)
#define	CE_TLR_REG		0x10
#define	 CE_TLR_HASH_RNG	__BIT(10)
#define	CE_ERR_REG		0x18
#define	 CE_ERR_CHAN0		__BITS(7,0)
#define	CE_LPC_REG		0xd0
#define	 CE_LPC_TRNG_TWO_SOURCES	__BIT(1)

/* The task descriptor of hash and random number tasks. */
#define	CE_TASK_COMM_CTL	0	/* 32 bits */
#define	 CE_COMM_CTL_LAST	__BIT(12)
#define	 CE_COMM_CTL_INTR	__BIT(16)
#define	CE_TASK_MAIN_CMD	4	/* 32 bits */
#define	 CE_MAIN_CMD_SHA256	3
#define	 CE_MAIN_CMD_TRNG	(2 << 8)
#define	CE_TASK_DST_ADDR	29	/* 40 bits, first output segment */
#define	CE_TASK_DST_LEN		40	/* 32 bits */
#define	CE_TASK_SIZE		208

#define	CE_RNG_BYTES		32
#define	CE_BUF_OFFSET		256
#define	CE_MEM_SIZE		PAGE_SIZE
#define	CE_TIMEOUT_US		100000
/* Tasks per request for entropy. */
#define	CE_RNG_ROUNDS		4

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-crypto" },
	DEVICE_COMPAT_EOL
};

struct sun60i_a733_crypto_softc {
	device_t		sc_dev;
	bus_space_tag_t		sc_bst;
	bus_space_handle_t	sc_bsh;
	bus_dma_tag_t		sc_dmat;
	bus_dmamap_t		sc_map;
	bus_dma_segment_t	sc_seg;
	uint8_t			*sc_mem;
	kmutex_t		sc_lock;
	krndsource_t		sc_rndsource;
};

#define	CE_READ(sc, reg)						\
	bus_space_read_4((sc)->sc_bst, (sc)->sc_bsh, (reg))
#define	CE_WRITE(sc, reg, val)						\
	bus_space_write_4((sc)->sc_bst, (sc)->sc_bsh, (reg), (val))

/* Run one task; the random bytes are then at CE_BUF_OFFSET. */
static int
sun60i_a733_crypto_rng(struct sun60i_a733_crypto_softc *sc)
{
	const bus_addr_t task = sc->sc_map->dm_segs[0].ds_addr;
	const bus_addr_t buf = task + CE_BUF_OFFSET;
	uint8_t *desc = sc->sc_mem;
	u_int waited;
	uint32_t err;

	memset(desc, 0, CE_TASK_SIZE);
	le32enc(desc + CE_TASK_COMM_CTL, CE_COMM_CTL_LAST | CE_COMM_CTL_INTR);
	le32enc(desc + CE_TASK_MAIN_CMD,
	    CE_MAIN_CMD_TRNG | CE_MAIN_CMD_SHA256);
	le32enc(desc + CE_TASK_DST_ADDR, (uint32_t)buf);
	desc[CE_TASK_DST_ADDR + 4] = (uint64_t)buf >> 32;
	le32enc(desc + CE_TASK_DST_LEN, CE_RNG_BYTES);
	bus_dmamap_sync(sc->sc_dmat, sc->sc_map, 0, CE_MEM_SIZE,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	CE_WRITE(sc, CE_ISR_REG, CE_ISR_CHAN0);
	CE_WRITE(sc, CE_TSK0_REG, (uint32_t)task);
	CE_WRITE(sc, CE_TSK1_REG, (uint64_t)task >> 32);
	CE_WRITE(sc, CE_TLR_REG, CE_TLR_HASH_RNG);

	for (waited = 0; (CE_READ(sc, CE_ISR_REG) & CE_ISR_CHAN0) == 0;
	    waited += 10) {
		if (waited >= CE_TIMEOUT_US)
			return ETIMEDOUT;
		delay(10);
	}
	err = CE_READ(sc, CE_ERR_REG) & CE_ERR_CHAN0;
	CE_WRITE(sc, CE_ISR_REG, CE_ISR_CHAN0);
	bus_dmamap_sync(sc->sc_dmat, sc->sc_map, 0, CE_MEM_SIZE,
	    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);

	return err != 0 ? EIO : 0;
}

static void
sun60i_a733_crypto_rng_get(size_t bytes, void *priv)
{
	struct sun60i_a733_crypto_softc * const sc = priv;
	uint8_t data[CE_RNG_BYTES];
	u_int n;

	for (n = 0; n < CE_RNG_ROUNDS && bytes > 0; n++) {
		mutex_enter(&sc->sc_lock);
		const int error = sun60i_a733_crypto_rng(sc);
		memcpy(data, sc->sc_mem + CE_BUF_OFFSET, sizeof(data));
		explicit_memset(sc->sc_mem + CE_BUF_OFFSET, 0, sizeof(data));
		mutex_exit(&sc->sc_lock);
		if (error != 0)
			break;
		/* The output is conditioned; count half of it as entropy. */
		rnd_add_data(&sc->sc_rndsource, data, sizeof(data),
		    sizeof(data) * NBBY / 2);
		bytes -= MIN(bytes, sizeof(data) / 2);
	}
	explicit_memset(data, 0, sizeof(data));
}

static int
sun60i_a733_crypto_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
sun60i_a733_crypto_attach(device_t parent, device_t self, void *aux)
{
	struct sun60i_a733_crypto_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	struct fdtbus_reset *rst;
	struct clk *clk;
	bus_addr_t addr;
	bus_size_t size;
	static const uint8_t zero[CE_RNG_BYTES];
	uint8_t first[CE_RNG_BYTES];
	int error, nsegs;
	u_int i;

	sc->sc_dev = self;
	sc->sc_bst = faa->faa_bst;
	sc->sc_dmat = faa->faa_dmat;
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_VM);

	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0) {
		aprint_error(": couldn't get registers\n");
		return;
	}
	for (i = 0; (clk = fdtbus_clock_get_index(phandle, i)) != NULL; i++) {
		if (clk_enable(clk) != 0) {
			aprint_error(": couldn't enable clock %u\n", i);
			return;
		}
	}
	if ((rst = fdtbus_reset_get_index(phandle, 0)) != NULL &&
	    fdtbus_reset_deassert(rst) != 0) {
		aprint_error(": couldn't de-assert reset\n");
		return;
	}
	if (bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_bsh) != 0) {
		aprint_error(": couldn't map registers\n");
		return;
	}

	aprint_naive("\n");
	aprint_normal(": Crypto engine (random numbers)\n");

	error = bus_dmamem_alloc(sc->sc_dmat, CE_MEM_SIZE, PAGE_SIZE, 0,
	    &sc->sc_seg, 1, &nsegs, BUS_DMA_WAITOK);
	if (error == 0)
		error = bus_dmamem_map(sc->sc_dmat, &sc->sc_seg, 1,
		    CE_MEM_SIZE, (void **)&sc->sc_mem, BUS_DMA_WAITOK);
	if (error == 0)
		error = bus_dmamap_create(sc->sc_dmat, CE_MEM_SIZE, 1,
		    CE_MEM_SIZE, 0, BUS_DMA_WAITOK, &sc->sc_map);
	if (error == 0)
		error = bus_dmamap_load(sc->sc_dmat, sc->sc_map, sc->sc_mem,
		    CE_MEM_SIZE, NULL, BUS_DMA_WAITOK);
	if (error != 0) {
		aprint_error_dev(self, "couldn't set up memory: %d\n", error);
		return;
	}

	/* The generator mixes two noise sources when told so. */
	CE_WRITE(sc, CE_LPC_REG,
	    CE_READ(sc, CE_LPC_REG) | CE_LPC_TRNG_TWO_SOURCES);
	/* Completion is polled; the flag still has to be let through. */
	CE_WRITE(sc, CE_ICR_REG, CE_READ(sc, CE_ICR_REG) | CE_ICR_CHAN0);

	/* Two answers in a row must be there and must differ. */
	if ((error = sun60i_a733_crypto_rng(sc)) != 0) {
		aprint_error_dev(self, "the generator does not answer: %d\n",
		    error);
		return;
	}
	memcpy(first, sc->sc_mem + CE_BUF_OFFSET, sizeof(first));
	if ((error = sun60i_a733_crypto_rng(sc)) != 0 ||
	    memcmp(first, sc->sc_mem + CE_BUF_OFFSET, sizeof(first)) == 0 ||
	    memcmp(first, zero, sizeof(first)) == 0) {
		aprint_error_dev(self, "the generator repeats itself\n");
		return;
	}
	explicit_memset(first, 0, sizeof(first));

	rndsource_setcb(&sc->sc_rndsource, sun60i_a733_crypto_rng_get, sc);
	rnd_attach_source(&sc->sc_rndsource, device_xname(self),
	    RND_TYPE_RNG, RND_FLAG_COLLECT_VALUE | RND_FLAG_HASCB);
}

CFATTACH_DECL_NEW(sun60i_a733_crypto, sizeof(struct sun60i_a733_crypto_softc),
    sun60i_a733_crypto_match, sun60i_a733_crypto_attach, NULL, NULL);
