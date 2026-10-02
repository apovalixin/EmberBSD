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

#ifndef _SUN60I_A733_CCU_H
#define _SUN60I_A733_CCU_H

/*
 * Clock and reset numbers follow the device tree binding
 * allwinner,sun60i-a733-ccu as posted for Linux (v3, September 2026).
 * Only the clocks this driver implements are listed.
 */

#define	A733_CLK_PLL_REF		0
#define	A733_CLK_SYS_24M		1
#define	A733_CLK_PLL_PERIPH0_4X		3
#define	A733_CLK_PLL_PERIPH0_2X		4
#define	A733_CLK_PLL_PERIPH0_800M	5
#define	A733_CLK_PLL_PERIPH0_480M	6
#define	A733_CLK_PLL_PERIPH0_600M	7
#define	A733_CLK_PLL_PERIPH0_400M	8
#define	A733_CLK_PLL_PERIPH0_300M	9
#define	A733_CLK_PLL_PERIPH0_200M	10
#define	A733_CLK_PLL_PERIPH0_160M	11
#define	A733_CLK_PLL_PERIPH0_150M	12
#define	A733_CLK_AHB			43
#define	A733_CLK_APB0			44
#define	A733_CLK_APB1			45
#define	A733_CLK_APB_UART		46
#define	A733_CLK_MBUS_GMAC0		91
#define	A733_CLK_MMC0			139
#define	A733_CLK_BUS_MMC0		140
#define	A733_CLK_MMC1			141
#define	A733_CLK_BUS_MMC1		142
#define	A733_CLK_MMC2			143
#define	A733_CLK_BUS_MMC2		144
#define	A733_CLK_BUS_UART0		150
#define	A733_CLK_BUS_UART1		151
#define	A733_CLK_BUS_UART2		152
#define	A733_CLK_BUS_UART3		153
#define	A733_CLK_BUS_UART4		154
#define	A733_CLK_BUS_UART5		155
#define	A733_CLK_BUS_UART6		156
#define	A733_CLK_BUS_I2C0		157
#define	A733_CLK_BUS_I2C1		158
#define	A733_CLK_BUS_I2C2		159
#define	A733_CLK_BUS_I2C3		160
#define	A733_CLK_BUS_THS0		184
#define	A733_CLK_GMAC0_PHY		227
#define	A733_CLK_BUS_GMAC0		228

#define	A733_RST_BUS_MMC0		34
#define	A733_RST_BUS_MMC1		35
#define	A733_RST_BUS_MMC2		36
#define	A733_RST_BUS_UART0		42
#define	A733_RST_BUS_UART1		43
#define	A733_RST_BUS_UART2		44
#define	A733_RST_BUS_UART3		45
#define	A733_RST_BUS_UART4		46
#define	A733_RST_BUS_UART5		47
#define	A733_RST_BUS_UART6		48
#define	A733_RST_BUS_I2C0		49
#define	A733_RST_BUS_I2C1		50
#define	A733_RST_BUS_I2C2		51
#define	A733_RST_BUS_I2C3		52
#define	A733_RST_BUS_THS0		69
#define	A733_RST_BUS_GMAC0		93
#define	A733_RST_BUS_GMAC0_AXI		94

#endif /* _SUN60I_A733_CCU_H */
