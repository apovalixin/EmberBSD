/* NetBSD controller setup. This program never resets a live controller. */
#include <sys/socket.h>
#include <sys/endian.h>
#include <bluetooth.h>
#include <err.h>
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static size_t
command(int fd, uint16_t opcode, void *parameter, size_t size,
    uint8_t *response, size_t capacity)
{
	struct bt_devreq req;

	memset(&req, 0, sizeof(req));
	req.opcode = opcode;
	req.cparam = parameter;
	req.clen = size;
	req.rparam = response;
	req.rlen = capacity;
	if (bt_devreq(fd, &req, 3) < 0)
		err(1, "controller command %04x", opcode);
	if (req.rlen < 1 || response[0] != 0)
		errx(1, "command %04x refused, status=%02x", opcode,
		    req.rlen ? response[0] : 0xff);
	return req.rlen;
}

static uint8_t
host_flag(int fd, uint16_t opcode)
{
	uint8_t response[16];

	if (command(fd, opcode, NULL, 0, response, sizeof(response)) != 2 ||
	    response[1] > 1)
		errx(1, "malformed host state for %04x", opcode);
	return response[1];
}

static void
check_sc(int fd)
{
	uint8_t page = 2, response[16];

	if (command(fd, 0x1004, &page, 1, response, sizeof(response)) != 11 ||
	    response[1] != 2 || (response[4] & 1) == 0)
		errx(1, "Secure Connections controller support not confirmed");
}

static void
security(int fd)
{
	uint8_t ssp, sc, enabled = 1, response[16];

	ssp = host_flag(fd, 0x0c55);
	sc = host_flag(fd, 0x0c79);
	check_sc(fd);
	/* Linux also enables SSP before SC. Preserve settings already live. */
	if (!ssp)
		command(fd, 0x0c56, &enabled, 1, response, sizeof(response));
	if (!sc)
		command(fd, 0x0c7a, &enabled, 1, response, sizeof(response));
	if (!host_flag(fd, 0x0c55) || !host_flag(fd, 0x0c79))
		errx(1, "SSP/SC enable readback failed");
	puts("SSP=1 SC=1");
}

static void
identity(int fd, const char *name)
{
	uint8_t eir[241] = {0}, response[255];
	uint8_t events[8] = {255, 255, 255, 255, 255, 255, 255, 31};
	size_t size = strlen(name);

	if (size < 1 || size > 200)
		errx(1, "Bluetooth name must contain 1 to 200 bytes");
	command(fd, 0x0c01, events, sizeof(events), response, sizeof(response));
	if (command(fd, 0x0c58, NULL, 0, response, sizeof(response)) != 2)
		errx(1, "malformed inquiry transmit power");
	eir[1] = size + 1;
	eir[2] = 0x09;
	memcpy(eir + 3, name, size);
	eir[size + 3] = 2;
	eir[size + 4] = 0x0a;
	eir[size + 5] = response[1];
	command(fd, 0x0c52, eir, sizeof(eir), response, sizeof(response));
	if (command(fd, 0x0c51, NULL, 0, response, sizeof(response)) != 242 ||
	    memcmp(response + 1, eir, sizeof(eir)) != 0)
		errx(1, "extended inquiry response readback failed");
	puts("EIR_NAME_AND_TX_POWER_VERIFIED");
}

static void
factory_address(bdaddr_t *address)
{
	struct ifaddrs *list, *entry;
	const struct sockaddr_dl *link;
	int found = 0;

	if (getifaddrs(&list) != 0)
		err(1, "read Wi-Fi interface address");
	for (entry = list; entry != NULL; entry = entry->ifa_next) {
		if (entry->ifa_addr == NULL ||
		    entry->ifa_addr->sa_family != AF_LINK ||
		    strcmp(entry->ifa_name, "bwfm0") != 0)
			continue;
		link = (const struct sockaddr_dl *)entry->ifa_addr;
		if (link->sdl_alen != 6)
			continue;
		for (unsigned i = 0; i < 6; i++)
			address->b[5 - i] = (uint8_t)LLADDR(link)[i];
		found = 1;
		break;
	}
	freeifaddrs(list);
	if (!found || (address->b[5] & 1))
		errx(1, "factory bwfm0 address unavailable");
	/* The observed Pi 5 has consecutive Wi-Fi and Bluetooth addresses. */
	for (unsigned i = 0; i < 6 && ++address->b[i] == 0; i++)
		continue;
}

static void
address(int fd, const char *configured)
{
	bdaddr_t desired;
	uint8_t response[16];

	if (configured != NULL) {
		if (!bt_aton(configured, &desired))
			errx(1, "invalid configured Bluetooth address");
	} else {
		factory_address(&desired);
	}
	if (command(fd, 0x1009, NULL, 0, response, sizeof(response)) != 7)
		errx(1, "malformed controller address");
	if (memcmp(response + 1, &desired, sizeof(desired)) == 0) {
		puts("ADDRESS_UNCHANGED");
		return;
	}
	/* Broadcom uses 0xfc01; NetBSD's unused 0xfc06 definition is wrong. */
	command(fd, 0xfc01, &desired, sizeof(desired), response, sizeof(response));
	if (command(fd, 0x1009, NULL, 0, response, sizeof(response)) != 7 ||
	    memcmp(response + 1, &desired, sizeof(desired)) != 0)
		errx(1, "Bluetooth address readback failed");
	puts("ADDRESS_CHANGED");
}

int
main(int argc, char **argv)
{
	int fd;

	if (argc < 3 || argc > 4)
		errx(1, "usage: bluetooth-control controller state|init|address [name|address]");
	if (strcmp(argv[2], "init") == 0 && argc != 4)
		errx(1, "init requires a Bluetooth name");
	fd = bt_devopen(argv[1], 0);
	if (fd < 0)
		err(1, "open controller");
	if (strcmp(argv[2], "state") == 0) {
		printf("SSP=%u SC=%u\n", host_flag(fd, 0x0c55), host_flag(fd, 0x0c79));
		check_sc(fd);
	} else if (strcmp(argv[2], "init") == 0) {
		security(fd);
		identity(fd, argv[3]);
	} else if (strcmp(argv[2], "address") == 0) {
		address(fd, argc == 4 ? argv[3] : NULL);
	} else {
		errx(1, "unknown action");
	}
	if (close(fd) != 0)
		err(1, "close controller");
	return 0;
}
