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
 * X-Powers AXP8191 power management chip: the power key.
 *
 * The chip switches the board on by itself when the key is pressed, and
 * the firmware switches it off through PSCI. What is left to the system
 * is to notice a short press while it runs and shut down in an orderly
 * way. The chip's interrupt line goes to a controller that has no
 * driver here, so the status is polled.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/callout.h>
#include <sys/device.h>
#include <sys/kernel.h>

#include <dev/i2c/i2cvar.h>

#include <dev/sysmon/sysmonvar.h>
#include <dev/sysmon/sysmon_taskq.h>

#include <dev/fdt/fdtvar.h>

#define	AXP8191_IC_TYPE		0x03
#define	AXP8191_CHIP_ID		0x0e
#define	AXP8191_CHIP_VER	0x0f
#define	AXP8191_IRQ_ENABLE2	0x41
#define	AXP8191_IRQ_STATUS2	0x49
#define	 AXP8191_IRQ2_POK_SHORT	__BIT(4)	/* the key, pressed briefly */

#define	AXP8191_POLL_MS		500

struct axp8191_softc {
	device_t		sc_dev;
	i2c_tag_t		sc_i2c;
	i2c_addr_t		sc_addr;

	struct sysmon_pswitch	sc_smpsw;
	callout_t		sc_tick;
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "x-powers,axp8191" },
	DEVICE_COMPAT_EOL
};

static int
axp8191_read(struct axp8191_softc *sc, uint8_t reg, uint8_t *val)
{
	int error;

	if ((error = iic_acquire_bus(sc->sc_i2c, 0)) != 0)
		return error;
	error = iic_smbus_read_byte(sc->sc_i2c, sc->sc_addr, reg, val, 0);
	iic_release_bus(sc->sc_i2c, 0);

	return error;
}

static int
axp8191_write(struct axp8191_softc *sc, uint8_t reg, uint8_t val)
{
	int error;

	if ((error = iic_acquire_bus(sc->sc_i2c, 0)) != 0)
		return error;
	error = iic_smbus_write_byte(sc->sc_i2c, sc->sc_addr, reg, val, 0);
	iic_release_bus(sc->sc_i2c, 0);

	return error;
}

/* The bus sleeps, so the status is read from the sysmon task queue. */
static void
axp8191_poll(void *arg)
{
	struct axp8191_softc * const sc = arg;
	uint8_t status;

	if (axp8191_read(sc, AXP8191_IRQ_STATUS2, &status) == 0 &&
	    (status & AXP8191_IRQ2_POK_SHORT) != 0) {
		/* A status bit is cleared by writing it back. */
		(void)axp8191_write(sc, AXP8191_IRQ_STATUS2,
		    AXP8191_IRQ2_POK_SHORT);
		sysmon_pswitch_event(&sc->sc_smpsw, PSWITCH_EVENT_PRESSED);
	}

	callout_schedule(&sc->sc_tick, mstohz(AXP8191_POLL_MS));
}

static void
axp8191_tick(void *arg)
{
	sysmon_task_queue_sched(0, axp8191_poll, arg);
}

static int
axp8191_match(device_t parent, cfdata_t match, void *aux)
{
	struct i2c_attach_args * const ia = aux;
	int match_result;

	if (iic_use_direct_match(ia, match, compat_data, &match_result))
		return match_result;

	return 0;
}

static void
axp8191_attach(device_t parent, device_t self, void *aux)
{
	struct axp8191_softc * const sc = device_private(self);
	struct i2c_attach_args * const ia = aux;
	uint8_t type, id, ver, enable;

	sc->sc_dev = self;
	sc->sc_i2c = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	aprint_naive("\n");
	if (axp8191_read(sc, AXP8191_IC_TYPE, &type) != 0 ||
	    axp8191_read(sc, AXP8191_CHIP_ID, &id) != 0 ||
	    axp8191_read(sc, AXP8191_CHIP_VER, &ver) != 0) {
		aprint_error(": the chip does not answer\n");
		return;
	}
	aprint_normal(": AXP8191, type 0x%02x, ID 0x%02x, version 0x%02x\n",
	    type, id, ver);

	/* Let the chip record a short press and drop what is recorded. */
	if (axp8191_read(sc, AXP8191_IRQ_ENABLE2, &enable) != 0 ||
	    axp8191_write(sc, AXP8191_IRQ_ENABLE2,
	    enable | AXP8191_IRQ2_POK_SHORT) != 0 ||
	    axp8191_write(sc, AXP8191_IRQ_STATUS2,
	    AXP8191_IRQ2_POK_SHORT) != 0) {
		aprint_error_dev(self, "couldn't set up the power key\n");
		return;
	}

	sysmon_task_queue_init();
	sc->sc_smpsw.smpsw_name = device_xname(self);
	sc->sc_smpsw.smpsw_type = PSWITCH_TYPE_POWER;
	if (sysmon_pswitch_register(&sc->sc_smpsw) != 0) {
		aprint_error_dev(self, "couldn't register the power key\n");
		return;
	}

	callout_init(&sc->sc_tick, CALLOUT_MPSAFE);
	callout_setfunc(&sc->sc_tick, axp8191_tick, sc);
	callout_schedule(&sc->sc_tick, mstohz(AXP8191_POLL_MS));
}

CFATTACH_DECL_NEW(axp8191pm, sizeof(struct axp8191_softc),
    axp8191_match, axp8191_attach, NULL, NULL);
