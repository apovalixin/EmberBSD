/* Origin: EmberBSD; AI-assisted outlined CAS argument-preservation seam. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <machine/asm.h>

/* x0/x1 are full-width expected/desired; x2 is storage; x3 is the helper. */
ENTRY_NP(ember_cas_call)
	br	x3
END(ember_cas_call)
.section .note.GNU-stack,"",%progbits
