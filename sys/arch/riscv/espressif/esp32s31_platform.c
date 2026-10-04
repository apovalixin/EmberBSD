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
 * ESP32-S31 platform: instruction cache maintenance.
 *
 * The caches in front of the flash and the PSRAM sit outside the core, are
 * split and are out of reach of fence.i.  Code the kernel has written to a
 * page becomes visible to instruction fetch only after the data cache is
 * written back and the instruction cache invalidated, which the firmware
 * does through a vendor SBI call.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>

#include <dev/fdt/fdtvar.h>

#include <uvm/uvm.h>
#include <uvm/pmap/pmap_devmap.h>

#include <machine/cpufunc.h>
#include <machine/sbi.h>

#include <riscv/fdt/riscv_fdtvar.h>

#define	SBI_EID_VENDOR_START	0x09000000
#define	SBI_EID_VENDOR_MASK	0x00ffffff

#define	S31_SBI_CACHE_WBACK		0
#define	S31_SBI_CACHE_INVAL		1
#define	S31_SBI_CACHE_WBACK_INVAL	2
#define	S31_SBI_ICACHE_SYNC		3
#define	S31_SBI_ICACHE_SYNC_RANGE	4

static int esp32s31_sbi_eid;

static void
esp32s31_icache_sync(struct vm_page_md *mdpg)
{
	if (mdpg == NULL) {
		SBI_CALL0(esp32s31_sbi_eid, S31_SBI_ICACHE_SYNC);
		return;
	}
	/* The firmware takes the address in the PSRAM window: the physical one. */
	const paddr_t pa = VM_PAGE_TO_PHYS(VM_MD_TO_PAGE(mdpg));
	SBI_CALL2(esp32s31_sbi_eid, S31_SBI_ICACHE_SYNC_RANGE, pa, PAGE_SIZE);
}

/*
 * The data cache in front of PSRAM is not coherent with bus masters such
 * as the Ethernet controller, and only the firmware can maintain it.
 * bus_dma reaches these through the outer-cache hooks, which carry the
 * physical address the firmware wants.
 */
/*
 * The ROM routines behind the firmware calls work on whole cache lines
 * and do nothing for a range shorter than one, so widen the range here.
 */
#define	S31_CACHE_LINE	64

static void
esp32s31_dcache_op(int func, paddr_t pa, psize_t len)
{
	const paddr_t start = pa & ~(paddr_t)(S31_CACHE_LINE - 1);
	const paddr_t end = roundup2(pa + len, S31_CACHE_LINE);

	SBI_CALL2(esp32s31_sbi_eid, func, start, end - start);
}

static void
esp32s31_dcache_wbinv(vaddr_t va, paddr_t pa, psize_t len)
{
	esp32s31_dcache_op(S31_SBI_CACHE_WBACK_INVAL, pa, len);
}

static void
esp32s31_dcache_inv(vaddr_t va, paddr_t pa, psize_t len)
{
	esp32s31_dcache_op(S31_SBI_CACHE_INVAL, pa, len);
}

static void
esp32s31_dcache_wb(vaddr_t va, paddr_t pa, psize_t len)
{
	esp32s31_dcache_op(S31_SBI_CACHE_WBACK, pa, len);
}

static const struct pmap_devmap *
esp32s31_platform_devmap(void)
{
	static const struct pmap_devmap devmap[] = {
		DEVMAP_ENTRY_END
	};

	return devmap;
}

static void
esp32s31_platform_bootstrap(void)
{
	riscv_fdt_cpu_bootstrap();

	esp32s31_sbi_eid = SBI_EID_VENDOR_START +
	    (sbi_get_mvendorid().value & SBI_EID_VENDOR_MASK);
	riscv_icache_sync = esp32s31_icache_sync;
	cpu_sdcache_wbinv_range = esp32s31_dcache_wbinv;
	cpu_sdcache_inv_range = esp32s31_dcache_inv;
	cpu_sdcache_wb_range = esp32s31_dcache_wb;
}

static u_int
esp32s31_platform_uart_freq(void)
{
	return 0;
}

static const struct fdt_platform esp32s31_platform = {
	.fp_devmap = esp32s31_platform_devmap,
	.fp_bootstrap = esp32s31_platform_bootstrap,
	.fp_uart_freq = esp32s31_platform_uart_freq,
	.fp_mpstart = riscv_fdt_cpu_mpstart,
};

FDT_PLATFORM(esp32s31, "esp,esp32s31", &esp32s31_platform);
