/* $NetBSD$ */

#ifndef _RISCV_ESPRESSIF_ESP_INTMTX_H_
#define _RISCV_ESPRESSIF_ESP_INTMTX_H_

/* For drivers that know their interrupt source without a device tree. */
void	*espintmtx_establish_source(u_int, int, int (*)(void *), void *,
	    const char *);
void	espintmtx_source_enable(u_int, bool);

#endif
