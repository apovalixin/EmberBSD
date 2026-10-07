/* Origin: EmberBSD; AI-assisted raw binary128 comparison ABI regression. */
/* SPDX-License-Identifier: BSD-2-Clause */
.text
.balign 4
.globl comparetf2_call
.type comparetf2_call, %function
comparetf2_call:
	stp x29, x30, [sp, #-32]!
	mov x29, sp
	str x19, [sp, #16]
	mov x19, x3
	mov x9, x0
	ldr q0, [x1]
	ldr q1, [x2]
	blr x9
	str w0, [x19]
	mrs x10, fpsr
	str x10, [x19, #8]
	mrs x10, fpcr
	str x10, [x19, #16]
	ldr x19, [sp, #16]
	ldp x29, x30, [sp], #32
	ret
.size comparetf2_call, .-comparetf2_call

.globl comparetf2_capture
.type comparetf2_capture, %function
comparetf2_capture:
	adrp x9, comparetf2_capture_a
	add x9, x9, :lo12:comparetf2_capture_a
	str q0, [x9]
	adrp x9, comparetf2_capture_b
	add x9, x9, :lo12:comparetf2_capture_b
	str q1, [x9]
	mov w0, #0
	ret
.size comparetf2_capture, .-comparetf2_capture
.bss
.balign 16
.globl comparetf2_capture_a
.hidden comparetf2_capture_a
comparetf2_capture_a:
	.skip 16
.globl comparetf2_capture_b
.hidden comparetf2_capture_b
comparetf2_capture_b:
	.skip 16
.section .note.GNU-stack,"",%progbits
