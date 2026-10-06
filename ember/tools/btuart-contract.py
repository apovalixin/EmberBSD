#!/usr/bin/env python3
"""Test the actual NetBSD H4 receiver with empty and ordinary wire packets.

Run on NetBSD: btuart-contract.py PATH_TO_SYS_DEV_BLUETOOTH_BTUART_C
Only allocation and HCI delivery are replaced; the parser is compiled from
the supplied source. This checks framing, not UART timing or the kernel ABI.
"""
import pathlib
import subprocess
import sys
import tempfile

if len(sys.argv) != 2 or not sys.platform.startswith("netbsd"):
    raise SystemExit("usage on NetBSD: btuart-contract.py BTUART_C")
source = pathlib.Path(sys.argv[1]).read_text()
function = source[source.index("static int\nbtuartinput("):source.index("static int\nbtuartstart(")]
states = source[source.index("/* sc_state */"):source.index("static int btuart_match")]
prefix = r'''
#include <sys/endian.h>
#include <bluetooth.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TTY_CHARMASK 0xff
#define M_DONTWAIT 0
#define MT_DATA 0
#define MHLEN 64
#define MLEN 64
#define MINCLSIZE 128
#define MCLBYTES 2048
#define M_EXT 1
struct mbuf {
    struct mbuf *m_next;
    struct { int len; } m_pkthdr;
    int m_len, m_flags;
    uint8_t data[2048];
};
#define mtod(m, type) ((type)((m)->data))
#define M_TRAILINGSPACE(m) (MHLEN - (m)->m_len)
#define MGETHDR(m, wait, type) ((m) = calloc(1, sizeof(*(m))))
#define MGET(m, wait, type) MGETHDR(m, wait, type)
#define MCLGET(m, wait) ((m)->m_flags |= M_EXT)
static void m_freem(struct mbuf *m) {
    while (m) { struct mbuf *next = m->m_next; free(m); m = next; }
}
struct btuart_softc {
    const char *sc_dev;
    bool sc_enabled;
    void *sc_unit;
    struct bt_stats sc_stats;
    int sc_state, sc_want;
    struct mbuf *sc_rxp;
};
struct tty { struct btuart_softc *t_sc; };
static uint8_t delivered[16][260];
static unsigned lengths[16], delivered_count;
static bool receive(void *unit, struct mbuf *m) {
    (void)unit;
    unsigned off = 0;
    assert(delivered_count < 16);
    for (struct mbuf *p = m; p; p = p->m_next) {
        assert(off + (unsigned)p->m_len <= 260);
        memcpy(delivered[delivered_count] + off, p->data, p->m_len);
        off += p->m_len;
    }
    lengths[delivered_count++] = off;
    m_freem(m);
    return true;
}
#define hci_input_acl receive
#define hci_input_sco receive
#define hci_input_event receive
#define aprint_error_dev(dev, ...) fprintf(stderr, __VA_ARGS__)
#define device_xname(dev) (dev)
#define panic(...) do { fprintf(stderr, __VA_ARGS__); abort(); } while (0)
'''
tests = r'''
static const uint8_t connection[] = {
    4, 3, 11, 0, 11, 0, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 1, 0
};
static int check(const char *name, const uint8_t *first, size_t first_len) {
    struct btuart_softc sc = { .sc_dev = "test", .sc_enabled = true };
    struct tty tp = { .t_sc = &sc };
    delivered_count = 0;
    for (size_t i = 0; i < first_len; i++) btuartinput(first[i], &tp);
    for (size_t i = 0; i < sizeof(connection); i++)
        btuartinput(connection[i], &tp);
    int good = delivered_count == 2 && lengths[0] == first_len &&
        memcmp(delivered[0], first, first_len) == 0 &&
        lengths[1] == sizeof(connection) &&
        memcmp(delivered[1], connection, sizeof(connection)) == 0 &&
        sc.sc_rxp == NULL && sc.sc_stats.err_rx == 0;
    printf("%s: %s (packets=%u errors=%u)\n", name,
        good ? "PASS" : "FAIL", delivered_count, sc.sc_stats.err_rx);
    m_freem(sc.sc_rxp);
    return !good;
}
int main(void) {
    /* Empty ACL packet observed on the BCM4345C0 before Connection Complete. */
    const uint8_t acl_empty[] = { 2, 11, 0x10, 0, 0 };
    const uint8_t sco_empty[] = { 3, 11, 0, 0 };
    const uint8_t event_empty[] = { 4, 0xff, 0 };
    const uint8_t normal_event[] = { 4, 0x0e, 4, 1, 3, 0x0c, 0 };
    const uint8_t normal_acl[] = { 2, 11, 0x20, 4, 0, 0, 0, 1, 0 };
    return check("empty ACL preserves next event", acl_empty, sizeof(acl_empty)) |
        check("empty SCO preserves next event", sco_empty, sizeof(sco_empty)) |
        check("empty event preserves next event", event_empty, sizeof(event_empty)) |
        check("normal event preserves next event", normal_event, sizeof(normal_event)) |
        check("normal ACL preserves next event", normal_acl, sizeof(normal_acl));
}
'''
with tempfile.TemporaryDirectory(prefix="btuart-contract-") as directory:
    test = pathlib.Path(directory) / "contract.c"
    test.write_text(prefix + states + function + tests)
    binary = test.with_suffix("")
    subprocess.run(["cc", "-std=c99", "-O0", "-Wall", "-Wextra", "-Werror",
                    str(test), "-o", str(binary)], check=True)
    sys.exit(subprocess.run([str(binary)]).returncode)
