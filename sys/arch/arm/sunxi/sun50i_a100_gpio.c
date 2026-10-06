/*-
 * Copyright (c) 2026 Anton and EmberBSD contributors
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
 * Pins of the Allwinner A100 and A133.
 *
 * The function numbers are the ones a running A133 board uses. Pin
 * interrupts are not described: the pins route them through function 6,
 * and nothing here needs them yet.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/types.h>

#include <arm/sunxi/sunxi_gpio.h>

static const struct sunxi_gpio_pins a100_pins[] = {
	{ "PB0",  1, 0,  { "gpio_in", "gpio_out", "uart2", "spi2" } },
	{ "PB1",  1, 1,  { "gpio_in", "gpio_out", "uart2", "spi2" } },
	{ "PB2",  1, 2,  { "gpio_in", "gpio_out", NULL, "spi2" } },
	{ "PB3",  1, 3,  { "gpio_in", "gpio_out", NULL, "spi2" } },
	{ "PB4",  1, 4,  { "gpio_in", "gpio_out", "i2c1" } },
	{ "PB5",  1, 5,  { "gpio_in", "gpio_out", "i2c1" } },
	{ "PB6",  1, 6,  { "gpio_in", "gpio_out" } },
	{ "PB7",  1, 7,  { "gpio_in", "gpio_out" } },
	{ "PB8",  1, 8,  { "gpio_in", "gpio_out" } },
	{ "PB9",  1, 9,  { "gpio_in", "gpio_out", "uart0", "i2c0" } },
	{ "PB10", 1, 10, { "gpio_in", "gpio_out", "uart0", "i2c0" } },

	{ "PC0",  2, 0,  { "gpio_in", "gpio_out", NULL, "mmc2" } },
	{ "PC1",  2, 1,  { "gpio_in", "gpio_out", NULL, "mmc2" } },
	{ "PC2",  2, 2,  { "gpio_in", "gpio_out", NULL, NULL, "spi0" } },
	{ "PC3",  2, 3,  { "gpio_in", "gpio_out", NULL, NULL, "spi0" } },
	{ "PC4",  2, 4,  { "gpio_in", "gpio_out", NULL, NULL, "spi0" } },
	{ "PC5",  2, 5,  { "gpio_in", "gpio_out", NULL, "mmc2" } },
	{ "PC6",  2, 6,  { "gpio_in", "gpio_out", NULL, "mmc2" } },
	{ "PC7",  2, 7,  { "gpio_in", "gpio_out", NULL, NULL, "spi0" } },
	{ "PC8",  2, 8,  { "gpio_in", "gpio_out", NULL, "mmc2" } },
	{ "PC9",  2, 9,  { "gpio_in", "gpio_out", NULL, "mmc2" } },
	{ "PC10", 2, 10, { "gpio_in", "gpio_out", NULL, "mmc2" } },
	{ "PC11", 2, 11, { "gpio_in", "gpio_out", NULL, "mmc2" } },
	{ "PC12", 2, 12, { "gpio_in", "gpio_out", NULL, NULL, "spi0" } },
	{ "PC13", 2, 13, { "gpio_in", "gpio_out", NULL, "mmc2" } },
	{ "PC14", 2, 14, { "gpio_in", "gpio_out", NULL, "mmc2" } },
	{ "PC15", 2, 15, { "gpio_in", "gpio_out", NULL, "mmc2", "spi0" } },
	{ "PC16", 2, 16, { "gpio_in", "gpio_out", NULL, "mmc2", "spi0" } },

	{ "PD0",  3, 0,  { "gpio_in", "gpio_out" } },
	{ "PD1",  3, 1,  { "gpio_in", "gpio_out" } },
	{ "PD2",  3, 2,  { "gpio_in", "gpio_out" } },
	{ "PD3",  3, 3,  { "gpio_in", "gpio_out" } },
	{ "PD4",  3, 4,  { "gpio_in", "gpio_out" } },
	{ "PD5",  3, 5,  { "gpio_in", "gpio_out" } },
	{ "PD6",  3, 6,  { "gpio_in", "gpio_out" } },
	{ "PD7",  3, 7,  { "gpio_in", "gpio_out" } },
	{ "PD8",  3, 8,  { "gpio_in", "gpio_out" } },
	{ "PD9",  3, 9,  { "gpio_in", "gpio_out" } },
	{ "PD10", 3, 10, { "gpio_in", "gpio_out", NULL, NULL, "spi1" } },
	{ "PD11", 3, 11, { "gpio_in", "gpio_out", NULL, NULL, "spi1" } },
	{ "PD12", 3, 12, { "gpio_in", "gpio_out", NULL, NULL, "spi1" } },
	{ "PD13", 3, 13, { "gpio_in", "gpio_out", NULL, NULL, "spi1" } },
	{ "PD14", 3, 14, { "gpio_in", "gpio_out" } },
	{ "PD15", 3, 15, { "gpio_in", "gpio_out" } },
	{ "PD16", 3, 16, { "gpio_in", "gpio_out" } },
	{ "PD17", 3, 17, { "gpio_in", "gpio_out" } },
	{ "PD18", 3, 18, { "gpio_in", "gpio_out", NULL, NULL, "uart4" } },
	{ "PD19", 3, 19, { "gpio_in", "gpio_out", NULL, NULL, "uart4" } },
	{ "PD20", 3, 20, { "gpio_in", "gpio_out", NULL, NULL, "uart4" } },
	{ "PD21", 3, 21, { "gpio_in", "gpio_out", NULL, NULL, "uart4" } },
	{ "PD22", 3, 22, { "gpio_in", "gpio_out" } },
	{ "PD23", 3, 23, { "gpio_in", "gpio_out" } },

	{ "PE0",  4, 0,  { "gpio_in", "gpio_out" } },
	{ "PE1",  4, 1,  { "gpio_in", "gpio_out", "i2c2" } },
	{ "PE2",  4, 2,  { "gpio_in", "gpio_out", "i2c2" } },
	{ "PE3",  4, 3,  { "gpio_in", "gpio_out" } },
	{ "PE4",  4, 4,  { "gpio_in", "gpio_out" } },
	{ "PE5",  4, 5,  { "gpio_in", "gpio_out" } },
	{ "PE6",  4, 6,  { "gpio_in", "gpio_out" } },
	{ "PE7",  4, 7,  { "gpio_in", "gpio_out" } },
	{ "PE8",  4, 8,  { "gpio_in", "gpio_out" } },
	{ "PE9",  4, 9,  { "gpio_in", "gpio_out" } },

	{ "PF0",  5, 0,  { "gpio_in", "gpio_out", "mmc0" } },
	{ "PF1",  5, 1,  { "gpio_in", "gpio_out", "mmc0" } },
	{ "PF2",  5, 2,  { "gpio_in", "gpio_out", "mmc0", "uart0" } },
	{ "PF3",  5, 3,  { "gpio_in", "gpio_out", "mmc0" } },
	{ "PF4",  5, 4,  { "gpio_in", "gpio_out", "mmc0", "uart0" } },
	{ "PF5",  5, 5,  { "gpio_in", "gpio_out", "mmc0" } },
	{ "PF6",  5, 6,  { "gpio_in", "gpio_out" } },

	{ "PG0",  6, 0,  { "gpio_in", "gpio_out", "mmc1" } },
	{ "PG1",  6, 1,  { "gpio_in", "gpio_out", "mmc1" } },
	{ "PG2",  6, 2,  { "gpio_in", "gpio_out", "mmc1" } },
	{ "PG3",  6, 3,  { "gpio_in", "gpio_out", "mmc1" } },
	{ "PG4",  6, 4,  { "gpio_in", "gpio_out", "mmc1" } },
	{ "PG5",  6, 5,  { "gpio_in", "gpio_out", "mmc1" } },
	{ "PG6",  6, 6,  { "gpio_in", "gpio_out", "uart1" } },
	{ "PG7",  6, 7,  { "gpio_in", "gpio_out", "uart1" } },
	{ "PG8",  6, 8,  { "gpio_in", "gpio_out", "uart1" } },
	{ "PG9",  6, 9,  { "gpio_in", "gpio_out", "uart1" } },
	{ "PG10", 6, 10, { "gpio_in", "gpio_out" } },
	{ "PG11", 6, 11, { "gpio_in", "gpio_out" } },
	{ "PG12", 6, 12, { "gpio_in", "gpio_out" } },
	{ "PG13", 6, 13, { "gpio_in", "gpio_out" } },

	{ "PH0",  7, 0,  { "gpio_in", "gpio_out", NULL, NULL, NULL, "emac" } },
	{ "PH1",  7, 1,  { "gpio_in", "gpio_out", NULL, NULL, NULL, "emac" } },
	{ "PH2",  7, 2,  { "gpio_in", "gpio_out", NULL, NULL, NULL, "emac" } },
	{ "PH3",  7, 3,  { "gpio_in", "gpio_out", NULL, NULL, NULL, "emac" } },
	{ "PH4",  7, 4,  { "gpio_in", "gpio_out", "uart3", NULL, NULL, "emac" } },
	{ "PH5",  7, 5,  { "gpio_in", "gpio_out", "uart3", NULL, NULL, "emac" } },
	{ "PH6",  7, 6,  { "gpio_in", "gpio_out", "uart3", NULL, NULL, "emac" } },
	{ "PH7",  7, 7,  { "gpio_in", "gpio_out", "uart3", NULL, NULL, "emac" } },
	{ "PH8",  7, 8,  { "gpio_in", "gpio_out" } },
	{ "PH9",  7, 9,  { "gpio_in", "gpio_out", NULL, NULL, NULL, "emac" } },
	{ "PH10", 7, 10, { "gpio_in", "gpio_out", NULL, NULL, NULL, "emac" } },
	{ "PH11", 7, 11, { "gpio_in", "gpio_out" } },
	{ "PH12", 7, 12, { "gpio_in", "gpio_out", NULL, "i2c3" } },
	{ "PH13", 7, 13, { "gpio_in", "gpio_out", NULL, "i2c3" } },
	{ "PH14", 7, 14, { "gpio_in", "gpio_out" } },
	{ "PH15", 7, 15, { "gpio_in", "gpio_out" } },
	{ "PH16", 7, 16, { "gpio_in", "gpio_out" } },
	{ "PH17", 7, 17, { "gpio_in", "gpio_out" } },
	{ "PH18", 7, 18, { "gpio_in", "gpio_out" } },
	{ "PH19", 7, 19, { "gpio_in", "gpio_out" } },

	{ "PI0",  8, 0,  { "gpio_in", "gpio_out", "i2c4" } },
	{ "PI1",  8, 1,  { "gpio_in", "gpio_out", "i2c4" } },
	{ "PI2",  8, 2,  { "gpio_in", "gpio_out", "uart5" } },
	{ "PI3",  8, 3,  { "gpio_in", "gpio_out", "uart5" } },
	{ "PI4",  8, 4,  { "gpio_in", "gpio_out", "uart5" } },
	{ "PI5",  8, 5,  { "gpio_in", "gpio_out", "uart5" } },
	{ "PI6",  8, 6,  { "gpio_in", "gpio_out", "uart6" } },
	{ "PI7",  8, 7,  { "gpio_in", "gpio_out", "uart6" } },
	{ "PI8",  8, 8,  { "gpio_in", "gpio_out", "i2c5" } },
	{ "PI9",  8, 9,  { "gpio_in", "gpio_out", "i2c5" } },
	{ "PI10", 8, 10, { "gpio_in", "gpio_out" } },
	{ "PI11", 8, 11, { "gpio_in", "gpio_out" } },
	{ "PI12", 8, 12, { "gpio_in", "gpio_out" } },
	{ "PI13", 8, 13, { "gpio_in", "gpio_out" } },
	{ "PI14", 8, 14, { "gpio_in", "gpio_out" } },

	{ "PJ0",  9, 0,  { "gpio_in", "gpio_out" } },
	{ "PJ1",  9, 1,  { "gpio_in", "gpio_out" } },
	{ "PJ2",  9, 2,  { "gpio_in", "gpio_out" } },
	{ "PJ3",  9, 3,  { "gpio_in", "gpio_out" } },
	{ "PJ4",  9, 4,  { "gpio_in", "gpio_out" } },
	{ "PJ5",  9, 5,  { "gpio_in", "gpio_out" } },
	{ "PJ6",  9, 6,  { "gpio_in", "gpio_out" } },
	{ "PJ7",  9, 7,  { "gpio_in", "gpio_out" } },
	{ "PJ8",  9, 8,  { "gpio_in", "gpio_out" } },
	{ "PJ9",  9, 9,  { "gpio_in", "gpio_out" } },
	{ "PJ10", 9, 10, { "gpio_in", "gpio_out" } },
	{ "PJ11", 9, 11, { "gpio_in", "gpio_out" } },
	{ "PJ12", 9, 12, { "gpio_in", "gpio_out" } },
	{ "PJ13", 9, 13, { "gpio_in", "gpio_out" } },
	{ "PJ14", 9, 14, { "gpio_in", "gpio_out" } },
	{ "PJ15", 9, 15, { "gpio_in", "gpio_out" } },
	{ "PJ16", 9, 16, { "gpio_in", "gpio_out" } },
	{ "PJ17", 9, 17, { "gpio_in", "gpio_out" } },
	{ "PJ18", 9, 18, { "gpio_in", "gpio_out" } },
	{ "PJ19", 9, 19, { "gpio_in", "gpio_out" } },
	{ "PJ20", 9, 20, { "gpio_in", "gpio_out" } },
	{ "PJ21", 9, 21, { "gpio_in", "gpio_out" } },
	{ "PJ22", 9, 22, { "gpio_in", "gpio_out" } },
	{ "PJ23", 9, 23, { "gpio_in", "gpio_out" } },
	{ "PJ24", 9, 24, { "gpio_in", "gpio_out" } },
	{ "PJ25", 9, 25, { "gpio_in", "gpio_out" } },
	{ "PJ26", 9, 26, { "gpio_in", "gpio_out" } },
	{ "PJ27", 9, 27, { "gpio_in", "gpio_out" } },
};

static const struct sunxi_gpio_pins a100_r_pins[] = {
	{ "PL0",  0, 0,  { "gpio_in", "gpio_out", "s_i2c0" } },
	{ "PL1",  0, 1,  { "gpio_in", "gpio_out", "s_i2c0" } },
	{ "PL2",  0, 2,  { "gpio_in", "gpio_out", "s_uart" } },
	{ "PL3",  0, 3,  { "gpio_in", "gpio_out", "s_uart" } },
	{ "PL4",  0, 4,  { "gpio_in", "gpio_out" } },
	{ "PL5",  0, 5,  { "gpio_in", "gpio_out" } },
	{ "PL6",  0, 6,  { "gpio_in", "gpio_out" } },
	{ "PL7",  0, 7,  { "gpio_in", "gpio_out" } },
	{ "PL8",  0, 8,  { "gpio_in", "gpio_out", NULL, "s_i2c1" } },
	{ "PL9",  0, 9,  { "gpio_in", "gpio_out", NULL, "s_i2c1" } },
	{ "PL10", 0, 10, { "gpio_in", "gpio_out" } },
	{ "PL11", 0, 11, { "gpio_in", "gpio_out", NULL, "s_cir" } },
};

const struct sunxi_gpio_padconf sun50i_a100_padconf = {
	.npins = __arraycount(a100_pins),
	.pins = a100_pins,
};

const struct sunxi_gpio_padconf sun50i_a100_r_padconf = {
	.npins = __arraycount(a100_r_pins),
	.pins = a100_r_pins,
};
