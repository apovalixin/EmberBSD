/* $NetBSD$ */

/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * Modelled on bcm2835_mbox_acpi.c.
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
 * The BCM2712 power management, reset and watchdog block described by
 * ACPI, as our build of the eotics-com rpi5-uefi firmware hands it over
 * (_HID PRP0001, _DSD compatible "brcm,bcm2712-pm").  The registers are
 * those of the BCM2835; bcm2835_pmwdog.c drives them.
 *
 * The same firmware copies the power source the Raspberry Pi boot
 * loader reports into \_SB.PSMC, PSRR, PSUH and PSOC (eotics names),
 * and into \_SB.BLRS the reset status register as the boot loader
 * found it: the register itself reads 0 by the time we run.  Together
 * they tell why the board last started.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>

#include <arm/broadcom/bcm2835_pmwdogvar.h>

/* A cell the boot loader did not report. */
#define	PM_ACPI_UNREPORTED	0xffffffff

/*
 * Why the PMIC was reset, bit by bit, as the Raspberry Pi configuration
 * reference documents power_reset in /chosen/power.
 */
static const char * const bcmpmwdog_acpi_pmic_reasons[] = {
	"over-voltage", "under-voltage", "over-temperature",
	"enable signal", "watchdog",
};

static int	bcmpmwdog_acpi_match(device_t, cfdata_t, void *);
static void	bcmpmwdog_acpi_attach(device_t, device_t, void *);
static void	bcmpmwdog_acpi_power_source(device_t);

CFATTACH_DECL_NEW(bcmpmwdog_acpi, sizeof(struct bcm2835pmwdog_softc),
    bcmpmwdog_acpi_match, bcmpmwdog_acpi_attach, NULL, NULL);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "brcm,bcm2712-pm" },
	DEVICE_COMPAT_EOL
};

static int
bcmpmwdog_acpi_match(device_t parent, cfdata_t cf, void *aux)
{
	struct acpi_attach_args *aa = aux;

	return acpi_compatible_match(aa, compat_data);
}

static void
bcmpmwdog_acpi_attach(device_t parent, device_t self, void *aux)
{
	struct bcm2835pmwdog_softc * const sc = device_private(self);
	struct acpi_attach_args *aa = aux;
	struct acpi_resources res;
	struct acpi_mem *mem;
	ACPI_STATUS rv;

	sc->sc_dev = self;
	sc->sc_iot = aa->aa_memt;

	rv = acpi_resource_parse(self, aa->aa_node->ad_handle, "_CRS",
	    &res, &acpi_resource_parse_ops_default);
	if (ACPI_FAILURE(rv))
		return;

	mem = acpi_res_mem(&res, 0);
	if (mem == NULL) {
		aprint_error_dev(self, "no registers\n");
		goto done;
	}
	if (bus_space_map(sc->sc_iot, mem->ar_base, mem->ar_length, 0,
	    &sc->sc_ioh) != 0) {
		aprint_error_dev(self, "couldn't map registers\n");
		goto done;
	}

	aprint_normal_dev(self, "power management, reset and watchdog\n");
	bcmpmwdog_attach_common(sc);
	bcmpmwdog_acpi_power_source(self);
done:
	acpi_resource_cleanup(&res);
}

static void
bcmpmwdog_acpi_power_source(device_t self)
{
	ACPI_INTEGER rsts, reset, current, high, overcurrent;
	const char *sep = " (";
	u_int i;

	if (ACPI_SUCCESS(acpi_eval_integer(NULL, "\\_SB.BLRS", &rsts)) &&
	    rsts != PM_ACPI_UNREPORTED)
		aprint_normal_dev(self, "reset status at boot %#jx\n",
		    (uintmax_t)rsts);

	if (ACPI_FAILURE(acpi_eval_integer(NULL, "\\_SB.PSRR", &reset)) ||
	    ACPI_FAILURE(acpi_eval_integer(NULL, "\\_SB.PSMC", &current)) ||
	    ACPI_FAILURE(acpi_eval_integer(NULL, "\\_SB.PSUH", &high)) ||
	    ACPI_FAILURE(acpi_eval_integer(NULL, "\\_SB.PSOC", &overcurrent)))
		return;

	aprint_normal_dev(self, "PMIC reset event");
	if (reset == PM_ACPI_UNREPORTED)
		aprint_normal(" not reported");
	else {
		aprint_normal(" %#jx", (uintmax_t)reset);
		for (i = 0; i < __arraycount(bcmpmwdog_acpi_pmic_reasons); i++) {
			if (!ISSET(reset, __BIT(i)))
				continue;
			aprint_normal("%s%s", sep,
			    bcmpmwdog_acpi_pmic_reasons[i]);
			sep = ", ";
		}
		if (sep[0] == ',')
			aprint_normal(")");
	}
	if (current != PM_ACPI_UNREPORTED)
		aprint_normal(", supply %ju mA", (uintmax_t)current);
	if (high != PM_ACPI_UNREPORTED)
		aprint_normal(", USB current limit %s", high ? "high" : "low");
	if (overcurrent != PM_ACPI_UNREPORTED && overcurrent != 0)
		aprint_normal(", USB over-current at boot");
	aprint_normal("\n");
}
