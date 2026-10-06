#!/bin/sh
# Exercise the actual PCI rescan function with a mock device and autoconf bus.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/virtio-pci-rescan.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cat > "$tmp/test.c" <<'EOF'
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

typedef void *device_t;
typedef void *cfdata_t;
struct virtio_softc { device_t sc_child; int sc_childdevid; };
struct virtio_pci_softc { struct virtio_softc sc_sc; };
struct virtio_attach_args { int sc_childdevid; };
#define CFARGS_NONE NULL
#define VIRTIO_CONFIG_DEVICE_STATUS_ACK 1
#define VIRTIO_CONFIG_DEVICE_STATUS_DRIVER 2
static char trace[32];
static int available, attach_ok;
static void event(char e) { size_t n = strlen(trace); assert(n + 1 < sizeof(trace)); trace[n] = e; trace[n + 1] = 0; }
static void *device_private(device_t d) { return d; }
static cfdata_t config_search(device_t d, void *v, void *args) {
    struct virtio_attach_args *va = v;
    (void)args;
    assert(va->sc_childdevid == 16);
    event('S');
    return available ? d : NULL;
}
static void virtio_device_reset(struct virtio_softc *sc) { (void)sc; event('R'); }
static void virtio_set_status(struct virtio_softc *sc, int s) {
    (void)sc;
    assert(s == 1 || s == 2);
    event(s == 1 ? 'A' : 'D');
}
static void config_attach(device_t d, cfdata_t cf, void *v, void *print, void *args) {
    struct virtio_pci_softc *psc = d;
    (void)v; (void)print; (void)args;
    assert(cf == d);
    event('T');
    if (attach_ok) psc->sc_sc.sc_child = d;
}
/* Also accept the old function, so the regression fails at runtime. */
static void config_found(device_t d, void *v, void *print, void *args) {
    cfdata_t cf = config_search(d, v, args);
    if (cf != NULL) config_attach(d, cf, v, print, args);
}
static int virtio_attach_failed(struct virtio_softc *sc) { event('F'); return sc->sc_child == NULL; }
EOF
awk '
    /^virtio_pci_rescan\(device_t self,/ { print "static int"; copy = 1 }
    copy { print }
    copy && /^}/ { found = 1; exit }
    END { if (!found) exit 1 }
' "$src/sys/dev/pci/virtio_pci.c" >> "$tmp/test.c"
cat >> "$tmp/test.c" <<'EOF'
int main(void) {
    struct virtio_pci_softc psc = {{ NULL, 16 }};
    available = 0;
    assert(virtio_pci_rescan(&psc, NULL, NULL) == 0);
    assert(strcmp(trace, "SF") == 0); /* Unclaimed firmware device stays intact. */
    trace[0] = 0; available = 1; attach_ok = 0;
    assert(virtio_pci_rescan(&psc, NULL, NULL) == 0);
    assert(strcmp(trace, "SRADTF") == 0); /* Reset precedes attempted ownership. */
    trace[0] = 0; attach_ok = 1;
    assert(virtio_pci_rescan(&psc, NULL, NULL) == 0);
    assert(strcmp(trace, "SRADTF") == 0); /* A later rescan initializes again. */
    trace[0] = 0;
    assert(virtio_pci_rescan(&psc, NULL, NULL) == 0);
    assert(trace[0] == 0); /* Never reset an attached child. */
    puts("virtio PCI rescan: unclaimed, failed, retry and attached cases passed");
    return 0;
}
EOF
${CC:-cc} -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
    "$tmp/test.c" -o "$tmp/test"
"$tmp/test"
