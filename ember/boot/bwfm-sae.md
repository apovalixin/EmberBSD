# External SAE in bwfm

This implementation connects upstream wpa_supplicant 2.11 SAE and its host
four-way handshake to the CYW43455 external-authentication firmware interface.
The OS repository owns the client, driver ABI and tests. Hardware acceptance
is pending; compilation and a firmware capability are not a working WPA3 claim.

The initial station mode supports SAE, CCMP and required PMF with
BIP-CMAC-128. It does not add fast transition, autonomous firmware roaming,
802.11v policy, SAE password offload or multi-link operation. Existing WPA2
connections keep their host handshake. Unsupported radios and old kernels
do not advertise SAE through the BSD driver.

## Interface and security boundaries

`IEEE80211_IOC_SAE` is additive. Version 1 uses fixed-width fields, a bounded
1536-byte payload, a peer address and a userspace association generation.
The getter returns capabilities only. Configuration, frame transmission,
authentication status and IGTK operations require interface administration
privilege. Authentication traffic is accepted only for the active peer and
generation while the interface is authenticating. Stale status cannot
authorize a new association. Passwords and PMKs remain in userspace.

`RTM_IEEE80211_SAE` forwards external-auth requests and received authentication
frames. The supplicant checks their sizes, peer and generation before passing
them to its upstream SAE implementation. Firmware provides CCMP and PMF;
the driver installs management keys at IGTK indexes 4 and 5.

## Build and checks

Build a matched kernel/modules bundle and base wpa_supplicant from the same
clean source revision using the [cross-build instructions](cross-build.md).
The client requires the existing OpenSSL-enabled build. Install its updated
net80211 headers into the target sysroot before compiling the client.
Do not combine a new client binary with an unrelated kernel acceptance claim.

The portable parser regression can run under address/undefined sanitizers:

```sh
cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
    ember/tools/bwfm-sae-contract.c -o /tmp/bwfm-sae-contract
/tmp/bwfm-sae-contract
```

Compile `ember/tools/bwfm-sae-ioctl.c` for NetBSD and run it as root on the
target interface with the supplicant stopped. It deliberately disables SAE
and tests invalid lengths, versions, reserved fields, inactive status and
unprivileged requests. Preserve Ethernet access during radio acceptance.

Acceptance requires an SAE-only client profile with `key_mgmt=SAE`,
`ieee80211w=2`, `pairwise=CCMP`, `group=CCMP`, and the original password in
`sae_password`. A WPA2-derived PSK is insufficient. Keep credentials in a
root-only device configuration. Verify SAE/PMF status, traffic, reconnect,
wrong-password rejection without WPA2 fallback, and a separate WPA2 regression.
Never enable supplicant key logging to collect public evidence.

## Provenance

The external-auth firmware protocol follows Raspberry Pi Linux
[`brcmfmac`](https://github.com/raspberrypi/linux/tree/43c132e8863c3bff3647033b6a7d2bf87b15501c/drivers/net/wireless/broadcom/brcm80211/brcmfmac),
specifically `cfg80211.c`, `fwil_types.h` and `fweh.h`. Those sources retain
Broadcom Corporation copyright (2010 and 2012) under ISC. EmberBSD implements
the protocol using its existing bwfm command transport and NetBSD interfaces;
the cryptographic SAE implementation remains upstream wpa_supplicant.
