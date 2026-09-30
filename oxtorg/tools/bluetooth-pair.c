/* Outgoing SSP for one selected peer; bthcid owns persistent link keys. */
#include <sys/socket.h>
#include <sys/endian.h>
#include <bluetooth.h>
#include <netbt/l2cap.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct pairing {
	bdaddr_t peer;
	uint16_t handle;
	uint32_t number;
	int have_handle, authenticated, encrypted, pending, just_works, legacy;
	int bonded_only;
	uint8_t key_type, cipher;
};

static time_t
monotonic(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		err(1, "read monotonic clock");
	return ts.tv_sec;
}

static int
pair_secured(const struct pairing *p)
{
	return p->have_handle && p->authenticated && p->encrypted;
}

static int
pair_confirm(struct pairing *p, int fd, int accepted)
{
	if (!p->pending)
		return -1;
	if (bt_devsend(fd, accepted ? 0x042c : 0x042d,
	    &p->peer, sizeof(p->peer)) < 0)
		return -1;
	p->pending = 0;
	puts(accepted ? "LOCAL_CONFIRMATION_SENT" : "LOCAL_CONFIRMATION_REJECTED");
	return 0;
}

static int
pair_event(struct pairing *p, int fd, const uint8_t *packet, size_t size)
{
	bdaddr_t address;
	uint8_t reply[9];
	uint16_t handle;
	uint32_t number;

	if (size < 3 || packet[0] != HCI_EVENT_PKT ||
	    size != (size_t)packet[2] + 3)
		return -1;
	switch (packet[1]) {
	case 0x03: /* Connection Complete */
		if (size != 14)
			return -1;
		memcpy(&address, packet + 6, sizeof(address));
		if (!bdaddr_same(&address, &p->peer))
			return 0;
		printf("CONNECTION_STATUS %02x\n", packet[3]);
		if (packet[3] != 0 || packet[12] != 1)
			return -1;
		memcpy(&handle, packet + 4, sizeof(handle));
		p->handle = le16toh(handle);
		p->have_handle = 1;
		p->authenticated = p->encrypted = 0;
		return 0;
	case 0x05: /* Disconnection Complete */
	case 0x06: /* Authentication Complete */
	case 0x08: /* Encryption Change */
		if (size != (packet[1] == 0x06 ? 6 : 7))
			return -1;
		memcpy(&handle, packet + 4, sizeof(handle));
		if (!p->have_handle || le16toh(handle) != p->handle)
			return 0;
		if (packet[1] == 0x05) {
			p->authenticated = p->encrypted = 0;
			return -1;
		}
		if (packet[1] == 0x06) {
			printf("AUTHENTICATION_STATUS %02x\n", packet[3]);
			p->authenticated = packet[3] == 0;
			return p->authenticated ? 0 : -1;
		}
		printf("ENCRYPTION_STATUS %02x mode=%u\n", packet[3], packet[6]);
		p->cipher = packet[3] == 0 ? packet[6] : 0;
		p->encrypted = packet[3] == 0 &&
		    (packet[6] == 2 || ((p->legacy || p->bonded_only) && packet[6] == 1));
		return p->encrypted ? 0 : -1;
	case 0x16: /* PIN Request: only bthcid's explicit legacy PIN is allowed. */
	case 0x18: /* Link Key Notification; never print the key bytes. */
	case 0x31: /* IO Capability Request */
	case 0x33: /* User Confirmation Request */
	case 0x36: /* Simple Pairing Complete */
		if (size != (packet[1] == 0x18 ? 26 :
		    (packet[1] == 0x31 || packet[1] == 0x16) ? 9 :
		    packet[1] == 0x33 ? 13 : 10))
			return -1;
		memcpy(&address, packet + (packet[1] == 0x36 ? 4 : 3),
		    sizeof(address));
		if (!bdaddr_same(&address, &p->peer))
			return 0;
		if (packet[1] == 0x16)
			return p->legacy ? 0 : -1;
		if (packet[1] == 0x18) {
			p->key_type = packet[25];
			printf("LINK_KEY_NOTIFICATION type=%u\n", p->key_type);
			return p->key_type == 8 ||
			    (p->just_works && p->key_type == 7) ||
			    (p->legacy && p->key_type == 0) ? 0 : -1;
		}
		if (packet[1] == 0x36) {
			printf("SIMPLE_PAIRING_STATUS %02x\n", packet[3]);
			return packet[3] == 0 ? 0 : -1;
		}
		if (packet[1] == 0x31) {
			if (p->legacy || p->bonded_only)
				return -1;
			memcpy(reply, &p->peer, sizeof(p->peer));
			reply[6] = p->just_works ? 3 : 1;
			reply[7] = 0;
			reply[8] = p->just_works ? 2 : 3; /* Dedicated bonding. */
			return bt_devsend(fd, 0x042b, reply, sizeof(reply)) < 0 ? -1 : 0;
		}
		memcpy(&number, packet + 9, sizeof(number));
		p->number = le32toh(number);
		if (p->number > 999999)
			return -1;
		p->pending = 1;
		if (p->just_works) {
			puts("JUST_WORKS_SELECTED_PEER no_numeric_MITM_protection");
			return pair_confirm(p, fd, 1);
		}
		printf("NUMERIC_COMPARISON %06u: compare on device; type yes or no\n",
		    p->number);
		return 0;
	default:
		return 0;
	}
}

static int
sdp_reply_valid(const uint8_t *reply, size_t size)
{
	size_t count, offset;

	if (size < 10 || reply[0] != 3 || reply[1] != 0x4b ||
	    reply[2] != 0x56 || be16dec(reply + 3) != size - 5)
		return 0;
	count = be16dec(reply + 7);
	if (count != 1 || be16dec(reply + 5) < count)
		return 0;
	offset = 9 + count * 4;
	return offset < size && reply[offset] <= 16 &&
	    size == offset + 1 + reply[offset];
}

static void
sdp_probe(int fd)
{
	/* Search one public L2CAP service handle; no contacts or messages. */
	uint8_t query[] = {2, 0x4b, 0x56, 0, 8, 0x35, 3, 0x19,
	    1, 0, 0, 1, 0};
	uint8_t reply[128];
	struct pollfd wait = { .fd = fd, .events = POLLIN };
	ssize_t size;

	if (send(fd, query, sizeof(query), 0) != sizeof(query))
		err(1, "send encrypted public SDP query");
	if (poll(&wait, 1, 5000) <= 0 || !(wait.revents & POLLIN))
		errx(1, "encrypted SDP reply timed out");
	size = recv(fd, reply, sizeof(reply), 0);
	if (size < 0 || !sdp_reply_valid(reply, (size_t)size))
		errx(1, "invalid encrypted SDP response");
	printf("ENCRYPTED_SDP_EXCHANGE bytes=%zd\n", size);
}

static void
require_security(int fd)
{
	struct bt_devreq req;
	uint8_t reply[2];
	uint16_t commands[] = {0x0c55, 0x0c79};

	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
		memset(&req, 0, sizeof(req));
		req.opcode = commands[i];
		req.rparam = reply;
		req.rlen = sizeof(reply);
		if (bt_devreq(fd, &req, 3) < 0 || req.rlen != 2 ||
		    reply[0] != 0 || reply[1] != 1)
			errx(1, "SSP/SC must be enabled by oxtorg_bluetooth first");
	}
}

int
main(int argc, char **argv)
{
	struct sockaddr_bt local = {0}, peer = {0};
	struct bt_devfilter filter = {0};
	struct pairing pairing = {0};
	struct pollfd wait[3];
	uint8_t packet[260];
	char answer[32], *end;
	socklen_t length;
	ssize_t size;
	time_t deadline;
	unsigned long psm = L2CAP_PSM_SDP;
	int fd, channel, mode, error, option, keep = 0, opened = 0;

	while ((option = getopt(argc, argv, "bjlk")) != -1) {
		switch (option) {
		case 'b': pairing.bonded_only = 1; break;
		case 'j': pairing.just_works = 1; break;
		case 'l': pairing.legacy = 1; break;
		case 'k': keep = 1; break;
		default: errx(1, "supported options: -j Just Works, -l legacy PIN, -k keep connection");
		}
	}
	if (pairing.just_works + pairing.legacy + pairing.bonded_only > 1)
		errx(1, "select one pairing mode: -b, -j, or -l");
	argc -= optind;
	argv += optind;
	if (argc < 2 || argc > 3)
		errx(1, "usage: bluetooth-pair [-b | -j | -l] [-k] controller peer-address [psm: 1 or 25]");
	if (argc == 3) {
		errno = 0;
		psm = strtoul(argv[2], &end, 0);
		if (errno != 0 || end == argv[2] || *end != '\0' ||
		    (psm != 1 && psm != 25))
			errx(1, "PSM must be 1 (SDP) or 25 (AVDTP)");
	}
	local.bt_len = peer.bt_len = sizeof(local);
	local.bt_family = peer.bt_family = AF_BLUETOOTH;
	peer.bt_psm = htole16(psm);
	if (!bt_devaddr(argv[0], &local.bt_bdaddr) ||
	    !bt_aton(argv[1], &peer.bt_bdaddr))
		errx(1, "invalid controller or peer");
	pairing.peer = peer.bt_bdaddr;
	fd = bt_devopen(argv[0], 0);
	if (fd < 0)
		err(1, "open controller");
	require_security(fd);
	bt_devfilter_pkt_set(&filter, HCI_EVENT_PKT);
	for (int event = 1; event <= 0xff; event++)
		bt_devfilter_evt_set(&filter, event);
	if (bt_devfilter(fd, &filter, NULL) != 0)
		err(1, "set HCI event filter");
	channel = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
	mode = L2CAP_LM_AUTH | L2CAP_LM_ENCRYPT;
	if (channel < 0 || setsockopt(channel, BTPROTO_L2CAP, SO_L2CAP_LM,
	    &mode, sizeof(mode)) != 0 ||
	    bind(channel, (struct sockaddr *)&local, sizeof(local)) != 0 ||
	    fcntl(channel, F_SETFL, O_NONBLOCK) != 0)
		err(1, "configure authenticated encrypted L2CAP channel");
	printf("OUTGOING_SSP_REQUEST peer=%s psm=%lu\n", argv[1], psm);
	fflush(stdout);
	if (connect(channel, (struct sockaddr *)&peer, sizeof(peer)) != 0 &&
	    errno != EINPROGRESS)
		err(1, "start outgoing connection");
	wait[0] = (struct pollfd){ .fd = fd, .events = POLLIN };
	wait[1] = (struct pollfd){ .fd = channel, .events = POLLOUT };
	wait[2] = (struct pollfd){ .fd = STDIN_FILENO, .events = POLLIN };
	deadline = monotonic() + 90;
	while (monotonic() < deadline) {
		if (poll(wait, 3, 500) < 0)
			err(1, "wait for pairing");
		if (wait[0].revents & POLLIN) {
			size = bt_devrecv(fd, packet, sizeof(packet), 1);
			if (size < 0 || pair_event(&pairing, fd, packet, size) < 0)
				errx(1, "HCI event rejected or security procedure failed");
			fflush(stdout);
		}
		if (wait[2].revents & (POLLIN | POLLHUP)) {
			if (!pairing.pending ||
			    fgets(answer, sizeof(answer), stdin) == NULL) {
				wait[2].fd = -1;
			} else if (strcmp(answer, "yes\n") == 0 ||
			    strcmp(answer, "no\n") == 0) {
				int accepted = strcmp(answer, "yes\n") == 0;
				if (pair_confirm(&pairing, fd, accepted) < 0 || !accepted)
					errx(1, "numeric comparison rejected");
				fflush(stdout);
			}
		}
		if (pairing.pending && wait[2].fd < 0) {
			(void)pair_confirm(&pairing, fd, 0);
			errx(1, "numeric comparison requires operator input");
		}
		if (!opened && wait[1].revents & (POLLOUT | POLLERR | POLLHUP)) {
			error = 0;
			length = sizeof(error);
			if (getsockopt(channel, SOL_SOCKET, SO_ERROR, &error, &length) < 0)
				err(1, "read connection result");
			if (error != 0) {
				errno = error;
				err(1, "outgoing protected channel");
			}
			opened = 1;
			wait[1].fd = -1;
		}
		/* NetBSD opens this socket only after the requested AUTH+ENCRYPT
		 * modes hold. A reused ACL need not emit new HCI security events. */
		if (opened && (!pairing.have_handle || pair_secured(&pairing)))
			break;
	}
	if (!opened || (pairing.have_handle && !pair_secured(&pairing)))
		errx(1, "authenticated encrypted connection not established within 90 seconds");
	if (!pairing.have_handle)
		puts("KERNEL_AUTH_ENCRYPT_L2CAP_OPEN cipher_not_observed");
	else
		puts(pairing.cipher == 2 ? "AUTHENTICATED_AES_CCM_L2CAP_OPEN" :
		    "AUTHENTICATED_ENCRYPTED_LEGACY_L2CAP_OPEN");
	if (psm == L2CAP_PSM_SDP)
		sdp_probe(channel);
	if (keep) {
		struct pollfd held = { .fd = channel, .events = POLLIN };
		puts("BLUETOOTH_SESSION_READY");
		fflush(stdout);
		for (;;) {
			int ready = poll(&held, 1, 15000);
			if (ready < 0 || held.revents != 0)
				errx(1, "Bluetooth session closed by peer");
			if (psm == L2CAP_PSM_SDP)
				sdp_probe(channel);
		}
	}
	if (close(channel) != 0 || close(fd) != 0)
		err(1, "close Bluetooth connection");
	return 0;
}
