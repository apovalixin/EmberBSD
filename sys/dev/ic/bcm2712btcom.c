/*
 * BCM2712 Bluetooth UART attachment for the observed Raspberry Pi 5 C1
 * layout. Firmware omits this port from ACPI. Reject other platforms
 * before MMIO and check the adjacent firmware pin layout.
 * GPIO28 (Wi-Fi power) is never owned by this attachment.
 */
#include <sys/param.h>
#include <sys/device.h>
#include <sys/module.h>
#include <sys/pmf.h>
#include <sys/systm.h>
#include <sys/termios.h>
#include <dev/acpi/acpivar.h>
#include <dev/ic/comvar.h>
#include <arm/pic/picvar.h>

MODULE(MODULE_CLASS_DRIVER, bcm2712btcom, NULL);

struct bcm2712btcom_softc {
	struct com_softc com;
	void *ih;
	bool attached;
};

static bus_space_tag_t bst;
static bus_space_handle_t uart, pins, gpio;
static uint32_t saved_mux, saved_pull, saved_data, saved_dir;
static device_t port;
static struct cfdata cf = {
	.cf_name = "com", .cf_atname = "bcm2712btcom", .cf_fstate = FSTATE_STAR,
};

static void
restore_pins(void)
{
	const uint32_t bit = 1U << 29, mask = 0x00f0ffff;
	uint32_t value;

	value = bus_space_read_4(bst, gpio, 0x504);
	bus_space_write_4(bst, gpio, 0x504, (value & ~bit) | (saved_data & bit));
	value = bus_space_read_4(bst, gpio, 0x508);
	bus_space_write_4(bst, gpio, 0x508, (value & ~bit) | (saved_dir & bit));
	value = bus_space_read_4(bst, pins, 0x10c);
	bus_space_write_4(bst, pins, 0x10c, (value & ~mask) | (saved_mux & mask));
	value = bus_space_read_4(bst, pins, 0x124);
	bus_space_write_4(bst, pins, 0x124,
	    (value & ~0x3fcU) | (saved_pull & 0x3fcU));
}

static void
bcm2712btcom_attach(device_t parent, device_t self, void *aux)
{
	struct bcm2712btcom_softc *sc = device_private(self);
	struct com_softc *com = &sc->com;

	com->sc_dev = self;
	com->sc_type = COM_TYPE_NORMAL;
	com->sc_frequency = 96000000;
	com_init_regs_stride_width(&com->sc_regs, bst, uart, 0x107d50c000ULL,
	    2, 4);
	/* Keep the interrupt masked until com has its handler. */
	bus_space_write_4(bst, uart, 4, 0);
	sc->ih = intr_establish_xname(308, IPL_SERIAL, IST_LEVEL, comintr, com,
	    device_xname(self));
	if (sc->ih == NULL) {
		aprint_error_dev(self, "cannot establish interrupt\n");
		return;
	}
	com_attach_subr(com);
	sc->attached = true;
	com->sc_hwflags |= COM_HW_AFE;
	/* The BCM7271 FIFO is 32 bytes; normal probing selects a safe 16. */
	if (!pmf_device_register(self, NULL, com_resume))
		aprint_error_dev(self, "cannot register power handler\n");
	aprint_normal_dev(self, "C1 Bluetooth UART, irq 308\n");
}

static int
bcm2712btcom_detach(device_t self, int flags)
{
	struct bcm2712btcom_softc *sc = device_private(self);
	int error;

	/* A busy tty must keep its interrupt and register mappings intact. */
	if (sc->attached) {
		error = com_detach(self, flags);
		if (error != 0)
			return error;
		pmf_device_deregister(self);
		sc->attached = false;
	}
	bus_space_write_4(bst, uart, 4, 0);
	if (sc->ih != NULL) {
		intr_disestablish(sc->ih);
		sc->ih = NULL;
	}
	return 0;
}

CFATTACH_DECL_NEW(bcm2712btcom, sizeof(struct bcm2712btcom_softc), NULL,
    bcm2712btcom_attach, bcm2712btcom_detach, NULL);

static int
bcm2712btcom_modcmd(modcmd_t cmd, void *arg)
{
	struct cfdriver *cd;
	const char *product;
	uint32_t value;
	int error;

	switch (cmd) {
	case MODULE_CMD_INIT:
		product = pmf_get_platform("system-product");
		if (product == NULL ||
		    strcmp(product, "Raspberry Pi 5 Model B") != 0)
			return ENXIO;
		if (acpi_softc == NULL)
			return ENXIO;
		bst = acpi_softc->sc_memt;
		error = bus_space_map(bst, 0x107d50c000ULL, 4096, 0, &uart);
		if (error != 0)
			return error;
		error = bus_space_map(bst, 0x107d504000ULL, 4096, 0, &pins);
		if (error != 0)
			goto unmap_uart;
		error = bus_space_map(bst, 0x107d508000ULL, 4096, 0, &gpio);
		if (error != 0)
			goto unmap_pins;
		saved_mux = bus_space_read_4(bst, pins, 0x10c);
		if ((saved_mux & 0xff000000) != 0x44000000 ||
		    (bus_space_read_4(bst, pins, 0x110) & 0xffff) != 0x3434) {
			error = ENXIO;
			goto unmap_gpio;
		}
		saved_pull = bus_space_read_4(bst, pins, 0x124);
		saved_data = bus_space_read_4(bst, gpio, 0x504);
		saved_dir = bus_space_read_4(bst, gpio, 0x508);
		cd = config_cfdriver_lookup("com");
		if (cd == NULL) {
			error = ENXIO;
			goto unmap_gpio;
		}
		for (cf.cf_unit = 0; cf.cf_unit < cd->cd_ndevs; cf.cf_unit++)
			if (device_lookup(cd, cf.cf_unit) == NULL)
				break;
		error = config_cfattach_attach("com", &bcm2712btcom_ca);
		if (error != 0)
			goto unmap_gpio;
		bus_space_write_4(bst, pins, 0x10c,
		    (saved_mux & ~0x00f0ffffU) | 0x4443);
		bus_space_write_4(bst, pins, 0x124,
		    (saved_pull & ~0x3fcU) | 0x220);
		bus_space_write_4(bst, gpio, 0x504, saved_data & ~(1U << 29));
		bus_space_write_4(bst, gpio, 0x508, saved_dir & ~(1U << 29));
		delay(100000);
		value = bus_space_read_4(bst, gpio, 0x504);
		bus_space_write_4(bst, gpio, 0x504, value | (1U << 29));
		delay(150000);
		port = config_attach_pseudo(&cf);
		if (port != NULL) {
			struct bcm2712btcom_softc *sc = device_private(port);
			if (sc->attached)
				return 0;
			config_detach(port, 0);
			port = NULL;
		}
		restore_pins();
		config_cfattach_detach("com", &bcm2712btcom_ca);
		error = ENXIO;
		goto unmap_gpio;
	case MODULE_CMD_FINI:
		error = config_detach(port, 0);
		if (error != 0)
			return error;
		restore_pins();
		config_cfattach_detach("com", &bcm2712btcom_ca);
		error = 0;
		goto unmap_gpio;
	default:
		return ENOTTY;
	}
unmap_gpio:
	bus_space_unmap(bst, gpio, 4096);
unmap_pins:
	bus_space_unmap(bst, pins, 4096);
unmap_uart:
	bus_space_unmap(bst, uart, 4096);
	return error;
}
