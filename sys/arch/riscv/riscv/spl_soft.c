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
 * Interrupt priority levels kept in software, for harts in CLIC mode.
 *
 * In CLIC mode sie and sip do not exist: the firmware emulates them as
 * plain storage, so they mask nothing.  Raising the level here only
 * records it.  An interrupt that arrives while its level is blocked is
 * masked at its controller and remembered; splx() unmasks it and the
 * level-triggered source fires again.  Uniprocessor only.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD$");

#include <sys/param.h>
#include <sys/cpu.h>
#include <sys/intr.h>
#include <sys/systm.h>

#include <machine/locore.h>
#include <machine/machdep.h>
#include <machine/sysreg.h>

#define	SPL_NSOURCES	32

static volatile uint32_t spl_pending;
static uint32_t spl_blocked[_IPL_N];
static void (*spl_maskfn[SPL_NSOURCES])(u_int, bool);

void softint_deliver(void);

void
riscv_spl_source(u_int source, int ipl, void (*maskfn)(u_int, bool))
{
	KASSERT(source < SPL_NSOURCES);
	KASSERT(ipl > IPL_NONE && ipl < _IPL_N);

	spl_maskfn[source] = maskfn;
	for (int l = ipl; l < _IPL_N; l++)
		spl_blocked[l] |= __BIT(source);

	if (curcpu()->ci_cpl < ipl)
		maskfn(source, false);
	else
		spl_pending |= __BIT(source);
}

/*
 * Called with interrupts disabled when <source> fires.  Returns false if
 * the current level blocks it; otherwise raises the level and hands the
 * previous one back for splx().
 */
bool
riscv_spl_intr_enter(u_int source, int ipl, int *pplp)
{
	struct cpu_info * const ci = curcpu();
	const int ppl = ci->ci_cpl;

	if (ipl <= ppl) {
		spl_maskfn[source](source, true);
		spl_pending |= __BIT(source);
		return false;
	}
	ci->ci_cpl = ipl;
	*pplp = ppl;
	return true;
}

static void
spl_unmask(int ipl)
{
	const register_t sr = csr_sstatus_read();

	csr_sstatus_clear(SR_SIE);
	uint32_t ready = spl_pending & ~spl_blocked[ipl];
	spl_pending &= ~ready;
	while (ready != 0) {
		const u_int source = ffs(ready) - 1;
		ready &= ~__BIT(source);
		spl_maskfn[source](source, false);
	}
	if (sr & SR_SIE)
		csr_sstatus_set(SR_SIE);
}

void
splx(int ipl)
{
	struct cpu_info * const ci = curcpu();

	if (ipl >= ci->ci_cpl)
		return;
	ci->ci_cpl = ipl;
	if (__predict_false(spl_pending & ~spl_blocked[ipl]))
		spl_unmask(ipl);
#ifdef __HAVE_FAST_SOFTINTS
	if (ci->ci_softints >> ipl)
		softint_deliver();
#endif
}

void
spl0(void)
{
	struct cpu_info * const ci = curcpu();

	ci->ci_cpl = IPL_NONE;
	spl_unmask(IPL_NONE);
	csr_sstatus_set(SR_SIE);
#ifdef __HAVE_FAST_SOFTINTS
	softint_deliver();
#endif
}

int
splraise(int ipl)
{
	struct cpu_info * const ci = curcpu();
	const int opl = ci->ci_cpl;

	if (opl < ipl)
		ci->ci_cpl = ipl;
	return opl;
}

int
splsoftclock(void)
{
	return splraise(IPL_SOFTCLOCK);
}

int
splsoftbio(void)
{
	return splraise(IPL_SOFTBIO);
}

int
splsoftnet(void)
{
	return splraise(IPL_SOFTNET);
}

int
splsoftserial(void)
{
	return splraise(IPL_SOFTSERIAL);
}

int
splvm(void)
{
	return splraise(IPL_VM);
}

int
splsched(void)
{
	return splraise(IPL_SCHED);
}

int
splhigh(void)
{
	return splraise(IPL_HIGH);
}
