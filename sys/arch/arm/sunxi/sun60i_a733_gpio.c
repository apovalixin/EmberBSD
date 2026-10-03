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
 * Allwinner A733 pin controller: pin multiplexing and GPIO.
 *
 * On the main controller each bank owns 0x80 bytes, the first at 0x80;
 * the controller of the always-on domain keeps the older, denser layout.
 * A pin's function is four bits wide and comes straight from the device
 * tree ("allwinner,pinmux"), so no per-pin table is needed. sunxigpio(4)
 * knows neither.
 *
 * Pin interrupts are not handled.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/gpio.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>

#define	A733_GPIO_PINS_PER_BANK	32

#define	A733_GPIO_BANK(c, b)	((c)->bank_base + (c)->bank_size * (b))
#define	A733_GPIO_CFG(c, b, p)	(A733_GPIO_BANK(c, b) + 0x00 + 4 * ((p) / 8))
#define	 A733_GPIO_CFG_MASK(p)	(0xfU << (((p) % 8) * 4))
#define	  A733_GPIO_CFG_INPUT	0
#define	  A733_GPIO_CFG_OUTPUT	1
#define	A733_GPIO_DATA(c, b)	(A733_GPIO_BANK(c, b) + 0x10)
#define	A733_GPIO_DRV(c, b, p)	\
	(A733_GPIO_BANK(c, b) + (c)->drv_offset + 4 * ((p) / 8))
#define	 A733_GPIO_DRV_MASK(p)	(0xfU << (((p) % 8) * 4))
#define	A733_GPIO_PULL(c, b, p)	\
	(A733_GPIO_BANK(c, b) + (c)->pull_offset + 4 * ((p) / 16))
#define	 A733_GPIO_PULL_MASK(p)	(0x3U << (((p) % 16) * 2))
#define	  A733_GPIO_PULL_NONE	0
#define	  A733_GPIO_PULL_UP	1
#define	  A733_GPIO_PULL_DOWN	2

/*
 * The chip feeds bank PF, where the card sits, with 3.3 V or 1.8 V by
 * itself. Bit PF_POW_VAL follows the supply; which level means what
 * depends on the chip revision, so only its change is watched.
 */
#define	A733_PIO_POW_VAL	0x48
#define	 A733_PIO_POW_VAL_PF	__BIT(10)
#define	A733_PIO_PF_POW		0x70
#define	 A733_PIO_PF_POW_3V3	__BIT(0)

struct a733_gpio_config {
	char		first_bank;	/* letter of the first bank */
	u_int		nbanks;
	bus_size_t	bank_base;	/* registers of the first bank */
	bus_size_t	bank_size;
	bus_size_t	drv_offset;	/* drive strength, inside a bank */
	bus_size_t	pull_offset;	/* pull-up and pull-down */
	bool		pf_supply;	/* switches the supply of bank PF */
};

static const struct a733_gpio_config a733_pio_config = {
	.first_bank = 'A',
	.nbanks = 11,		/* PA to PK */
	.bank_base = 0x80,
	.bank_size = 0x80,
	.drv_offset = 0x20,
	.pull_offset = 0x30,
	.pf_supply = true,
};

static const struct a733_gpio_config a733_r_pio_config = {
	.first_bank = 'L',
	.nbanks = 2,		/* PL and PM */
	.bank_base = 0x00,
	.bank_size = 0x30,
	.drv_offset = 0x14,
	.pull_offset = 0x24,
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-pinctrl",
	  .data = &a733_pio_config },
	{ .compat = "allwinner,sun60i-a733-r-pinctrl",
	  .data = &a733_r_pio_config },
	DEVICE_COMPAT_EOL
};

struct a733_gpio_softc {
	device_t			sc_dev;
	bus_space_tag_t			sc_bst;
	bus_space_handle_t		sc_bsh;
	kmutex_t			sc_lock;
	const struct a733_gpio_config	*sc_config;
};

struct a733_gpio_pin {
	u_int	pin_bank;
	u_int	pin_num;
	bool	pin_actlo;
};

#define	GPIO_READ(sc, reg)		\
	bus_space_read_4((sc)->sc_bst, (sc)->sc_bsh, (reg))
#define	GPIO_WRITE(sc, reg, val)	\
	bus_space_write_4((sc)->sc_bst, (sc)->sc_bsh, (reg), (val))

static void
a733_gpio_update(struct a733_gpio_softc *sc, bus_size_t reg, uint32_t mask,
    u_int value)
{
	uint32_t val;

	KASSERT(mutex_owned(&sc->sc_lock));

	val = GPIO_READ(sc, reg);
	val &= ~mask;
	val |= __SHIFTIN(value, mask);
	GPIO_WRITE(sc, reg, val);
}

/* "PH12" on the main controller is bank 7, pin 12. */
static bool
a733_gpio_parse_pin(struct a733_gpio_softc *sc, const char *name,
    u_int *bank, u_int *pin)
{
	u_int n;

	if (name[0] != 'P' || name[1] < sc->sc_config->first_bank ||
	    name[2] < '0' || name[2] > '9')
		return false;
	*bank = name[1] - sc->sc_config->first_bank;
	if (*bank >= sc->sc_config->nbanks)
		return false;
	for (n = 0, name += 2; *name >= '0' && *name <= '9'; name++)
		n = n * 10 + (*name - '0');
	if (*name != '\0' || n >= A733_GPIO_PINS_PER_BANK)
		return false;
	*pin = n;

	return true;
}

static int
a733_pinctrl_set_config(device_t dev, const void *data, size_t len)
{
	struct a733_gpio_softc * const sc = device_private(dev);
	const struct a733_gpio_config * const c = sc->sc_config;
	u_int bank, pin, mux;
	int pins_len;

	if (len != 4)
		return -1;

	const int phandle = fdtbus_get_phandle_from_native(be32dec(data));

	const char *pins = fdtbus_pinctrl_parse_pins(phandle, &pins_len);
	if (pins == NULL)
		return -1;
	if (of_getprop_uint32(phandle, "allwinner,pinmux", &mux) != 0 ||
	    mux > 0xf)
		return -1;

	const int bias = fdtbus_pinctrl_parse_bias(phandle, NULL);
	const int drive_strength = fdtbus_pinctrl_parse_drive_strength(phandle);

	mutex_enter(&sc->sc_lock);

	for (; pins_len > 0;
	    pins_len -= strlen(pins) + 1, pins += strlen(pins) + 1) {
		if (!a733_gpio_parse_pin(sc, pins, &bank, &pin)) {
			aprint_error_dev(dev, "unknown pin name '%s'\n", pins);
			continue;
		}
		a733_gpio_update(sc, A733_GPIO_CFG(c, bank, pin),
		    A733_GPIO_CFG_MASK(pin), mux);

		if (bias != -1) {
			a733_gpio_update(sc, A733_GPIO_PULL(c, bank, pin),
			    A733_GPIO_PULL_MASK(pin),
			    bias == GPIO_PIN_PULLUP ? A733_GPIO_PULL_UP :
			    bias == GPIO_PIN_PULLDOWN ? A733_GPIO_PULL_DOWN :
			    A733_GPIO_PULL_NONE);
		}

		/* Drive strength comes in mA; a level is 10 mA. */
		if (drive_strength >= 10 && drive_strength <= 40) {
			a733_gpio_update(sc, A733_GPIO_DRV(c, bank, pin),
			    A733_GPIO_DRV_MASK(pin), drive_strength / 10 - 1);
		}
	}

	mutex_exit(&sc->sc_lock);

	return 0;
}

static struct fdtbus_pinctrl_controller_func a733_pinctrl_funcs = {
	.set_config = a733_pinctrl_set_config,
};

static void *
a733_gpio_acquire(device_t dev, const void *data, size_t len, int flags)
{
	struct a733_gpio_softc * const sc = device_private(dev);
	const struct a733_gpio_config * const c = sc->sc_config;
	struct a733_gpio_pin *gpin;
	const u_int *gpio = data;

	/* <&controller bank pin flags> */
	if (len != 16)
		return NULL;

	const u_int bank = be32toh(gpio[1]);
	const u_int pin = be32toh(gpio[2]);
	const bool actlo = be32toh(gpio[3]) & 1;

	if (bank >= sc->sc_config->nbanks || pin >= A733_GPIO_PINS_PER_BANK)
		return NULL;
	if ((flags & (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT)) == 0)
		return NULL;

	mutex_enter(&sc->sc_lock);
	a733_gpio_update(sc, A733_GPIO_CFG(c, bank, pin),
	    A733_GPIO_CFG_MASK(pin),
	    (flags & GPIO_PIN_INPUT) ? A733_GPIO_CFG_INPUT :
	    A733_GPIO_CFG_OUTPUT);
	mutex_exit(&sc->sc_lock);

	gpin = kmem_zalloc(sizeof(*gpin), KM_SLEEP);
	gpin->pin_bank = bank;
	gpin->pin_num = pin;
	gpin->pin_actlo = actlo;

	return gpin;
}

static void
a733_gpio_release(device_t dev, void *priv)
{
	struct a733_gpio_softc * const sc = device_private(dev);
	const struct a733_gpio_config * const c = sc->sc_config;
	struct a733_gpio_pin *gpin = priv;

	mutex_enter(&sc->sc_lock);
	a733_gpio_update(sc, A733_GPIO_CFG(c, gpin->pin_bank, gpin->pin_num),
	    A733_GPIO_CFG_MASK(gpin->pin_num), A733_GPIO_CFG_INPUT);
	mutex_exit(&sc->sc_lock);

	kmem_free(gpin, sizeof(*gpin));
}

static int
a733_gpio_read(device_t dev, void *priv, bool raw)
{
	struct a733_gpio_softc * const sc = device_private(dev);
	const struct a733_gpio_config * const c = sc->sc_config;
	struct a733_gpio_pin *gpin = priv;
	int val;

	val = __SHIFTOUT(GPIO_READ(sc, A733_GPIO_DATA(c, gpin->pin_bank)),
	    __BIT(gpin->pin_num));
	if (!raw && gpin->pin_actlo)
		val = !val;

	return val;
}

static void
a733_gpio_write(device_t dev, void *priv, int val, bool raw)
{
	struct a733_gpio_softc * const sc = device_private(dev);
	const struct a733_gpio_config * const c = sc->sc_config;
	struct a733_gpio_pin *gpin = priv;

	if (!raw && gpin->pin_actlo)
		val = !val;

	mutex_enter(&sc->sc_lock);
	a733_gpio_update(sc, A733_GPIO_DATA(c, gpin->pin_bank),
	    __BIT(gpin->pin_num), val ? 1 : 0);
	mutex_exit(&sc->sc_lock);
}

static struct fdtbus_gpio_controller_func a733_gpio_funcs = {
	.acquire = a733_gpio_acquire,
	.release = a733_gpio_release,
	.read = a733_gpio_read,
	.write = a733_gpio_write,
};

/*
 * The supply of bank PF as a regulator, so that the card driver can ask
 * for 1.8 V signalling.
 */
static int
a733_pf_supply_acquire(device_t dev)
{
	return 0;
}

static void
a733_pf_supply_release(device_t dev)
{
}

static int
a733_pf_supply_enable(device_t dev, bool enable)
{
	return enable ? 0 : EINVAL;
}

static int
a733_pf_supply_get_voltage(device_t dev, u_int *puvol)
{
	struct a733_gpio_softc * const sc = device_private(dev);

	*puvol = (GPIO_READ(sc, A733_PIO_PF_POW) & A733_PIO_PF_POW_3V3) ?
	    3300000 : 1800000;

	return 0;
}

static int
a733_pf_supply_set_voltage(device_t dev, u_int min_uvol, u_int max_uvol)
{
	struct a733_gpio_softc * const sc = device_private(dev);
	uint32_t before, pow;
	u_int cur;
	int retry;

	if (min_uvol <= 1800000 && 1800000 <= max_uvol)
		pow = 0;
	else if (min_uvol <= 3300000 && 3300000 <= max_uvol)
		pow = A733_PIO_PF_POW_3V3;
	else
		return ERANGE;

	a733_pf_supply_get_voltage(dev, &cur);
	if ((pow != 0) == (cur == 3300000))
		return 0;

	mutex_enter(&sc->sc_lock);
	before = GPIO_READ(sc, A733_PIO_POW_VAL) & A733_PIO_POW_VAL_PF;
	GPIO_WRITE(sc, A733_PIO_PF_POW, pow);
	mutex_exit(&sc->sc_lock);

	for (retry = 100; retry > 0; retry--) {
		if ((GPIO_READ(sc, A733_PIO_POW_VAL) & A733_PIO_POW_VAL_PF) !=
		    before)
			break;
		delay(100);
	}
	if (retry == 0) {
		device_printf(dev, "supply of bank PF did not change\n");
		return ETIMEDOUT;
	}
	delay(10);

	return 0;
}

static const struct fdtbus_regulator_controller_func a733_pf_supply_funcs = {
	.acquire = a733_pf_supply_acquire,
	.release = a733_pf_supply_release,
	.enable = a733_pf_supply_enable,
	.set_voltage = a733_pf_supply_set_voltage,
	.get_voltage = a733_pf_supply_get_voltage,
};

static int
a733_gpio_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
a733_gpio_attach(device_t parent, device_t self, void *aux)
{
	struct a733_gpio_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	struct clk *clk;
	bus_addr_t addr;
	bus_size_t size;
	int child;

	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0) {
		aprint_error(": couldn't get registers\n");
		return;
	}

	if ((clk = fdtbus_clock_get_index(phandle, 0)) != NULL &&
	    clk_enable(clk) != 0) {
		aprint_error(": couldn't enable clock\n");
		return;
	}

	sc->sc_dev = self;
	sc->sc_bst = faa->faa_bst;
	sc->sc_config = of_compatible_lookup(phandle, compat_data)->data;
	if (bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_bsh) != 0) {
		aprint_error(": couldn't map registers\n");
		return;
	}
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_VM);

	aprint_naive("\n");
	aprint_normal(": PIO, banks P%c to P%c\n", sc->sc_config->first_bank,
	    sc->sc_config->first_bank + sc->sc_config->nbanks - 1);

	fdtbus_register_gpio_controller(self, phandle, &a733_gpio_funcs);

	for (child = OF_child(phandle); child; child = OF_peer(child)) {
		if (sc->sc_config->pf_supply &&
		    of_hasprop(child, "regulator-name")) {
			aprint_normal_dev(self, "bank PF at %s V "
			    "(POW_VAL 0x%08x)\n",
			    (GPIO_READ(sc, A733_PIO_PF_POW) &
			    A733_PIO_PF_POW_3V3) ? "3.3" : "1.8",
			    GPIO_READ(sc, A733_PIO_POW_VAL));
			fdtbus_register_regulator_controller(self, child,
			    &a733_pf_supply_funcs);
			continue;
		}
		if (!of_hasprop(child, "pins") ||
		    !of_hasprop(child, "allwinner,pinmux"))
			continue;
		fdtbus_register_pinctrl_config(self, child,
		    &a733_pinctrl_funcs);
	}
}

CFATTACH_DECL_NEW(sun60i_a733_gpio, sizeof(struct a733_gpio_softc),
	a733_gpio_match, a733_gpio_attach, NULL, NULL);
