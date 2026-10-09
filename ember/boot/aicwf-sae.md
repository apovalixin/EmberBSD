# AIC8800D80 external SAE

The AIC SDIO path uses the existing EmberBSD external-SAE interface and
upstream wpa_supplicant 2.11. Authentication and the four-way handshake stay
in userspace. Firmware handles association, CCMP and protected management
frames. This implementation targets firmware 0x06090101 with its MFP feature
bit; other firmware revisions do not advertise SAE until separately checked.

## Current boundary

The station path accepts SAE, CCMP and required PMF with BIP-CMAC-128.
The firmware key command has no initial IGTK packet-number field. The
client therefore rejects a nonzero initial IPN instead of silently losing
the replay boundary. Networks/rekeys supplying such an IPN cannot complete
this path yet. On a physical Zero 3W, required PMF negotiation, SAE/H2E,
IGTK installation and bidirectional traffic pass. Management-frame forgery
and replay injection remain untested; this is not a WPA3 certification claim.

SAE configuration is privileged. A generation and peer bind commands to one
association. Authentication frames must have bounded length, the selected
AP's addresses and SAE algorithm; fragments, foreign peers and other frame
types are rejected. An internal lifetime counter also retires queued commands and late replies
after a disconnect or profile change. Firmware response status must be present. External-auth requests accept
the two SAE selector byte orders supported by upstream wpa_supplicant. IGTK hardware
slots are separate from the pairwise-data-key slot.

Use the [SAE profile and client build guide](bwfm-sae.md#configuration-and-verification),
substituting `aicwf0` for the interface. Install the matching kernel/modules
and current base wpa_supplicant. Preserve a bootable kernel and a recovery
path before replacing the only network connection. Keep credentials and
unredacted logs outside source control. `vmstat -e` shows the driver's SAE
start, received, sent and rejected counters without displaying credentials.

This work does not add 802.11r fast transition, 802.11v BSS transition
management, 802.11ac/ax or host AP mode. The [Zero 3W board page](../boards/orange-pi-zero-3w.md)
records actual physical results separately.

## Band steering and acceptance

A successful SAE handshake does not prove sustained data traffic. A mesh AP
can remove a 2.4 GHz station under its band-steering policy while the client
still reports `COMPLETED`. Compare the AP log with host-side SSH and packet
checks before attributing that failure to SAE or the kernel. In the physical
Zero 3W trial, the AP explicitly prohibited 2.4 GHz about a minute after joining;
the same policy removed WPA2 and WPA3 associations.

A 2026-10-09 diagnostic build reproduced the removal with the BSSID lock
cleared after association. The driver received repeated unprotected,
26-byte deauthentication frames but did not forward them to the supplicant.
No firmware disconnect indication arrived. A timed restart of the normal
supplicant service restored 5 GHz and an 8 MiB bidirectional transfer passed.
This is recovery by the test harness, not autonomous driver recovery.
The captured metadata does not include the reason code or frame addresses.

The host path currently lacks SA Query handling for an AP/STA state mismatch.
Do not convert an unprotected deauthentication frame directly into a trusted
disconnect. A complete fix must validate the peer and reason, use protected
SA Query requests and responses, and reject stale, replayed or unauthenticated
responses. Negotiated PMF alone does not validate that recovery path.

Diagnostic builds expose management-frame type, flags, length and hardware
status through `sysctl -w net.link.ieee80211.vap0.debug=524288` (verify that
`vap0.parent` is the intended interface). No frame body or key is logged.
Restore the previous debug value after the trial.

Do not pin the client to a BSSID that the AP's policy forbids. For a network
that requires 5 GHz, set the network profile's `freq_list` to its allowed
5 GHz frequencies, without a BSSID lock. Other network profiles can retain
2.4 GHz support. `freq_list` restricts candidate APs; it is not a preference
with a 2.4 GHz fallback. Preserve another profile if that fallback is needed.

When testing the only network interface, keep the recovery timer armed until
a separate host confirms SSH and bidirectional traffic after the operation.
A single successful gateway ping immediately after association is insufficient.

## Checks

```sh
sh ember/tests/aicwf-sae.sh
sh ember/tests/aicwf-scan.sh
sh ember/tests/aicwf-scan-channel.sh
sh ember/tests/aicwf-flow-control.sh
sh ember/tests/aicwf-command.sh
sh ember/tests/aicwf-sae-lifetime.sh
sh ember/tests/aicwf-sae-event.sh
```

The first runs bounded AIC request/frame/IGTK checks and the shared RSN
security checks also used by bwfm. The extracted-function regressions cover
connected scans, adjacent-channel beacons, the D80 free-buffer register,
short command replies, stale commands and confirmations,
CONFIGURE during requested leave, and allocation failure in SAE events.
For a live ABI check, build
`ember/tools/bwfm-sae-ioctl.c` with the current net80211 headers and run it as
root with `aicwf0`, with the supplicant stopped and recovery arranged.
It checks lengths, version/reserved fields, stale authorization and denial
of unprivileged configuration. Its historical filename does not restrict
the driver being tested.

## Protocol provenance

The wire layout was inspected in Radxa's AIC8800 SDIO driver at revision
[`d13d07963cd15d731e2895e8288a04cca6152ac9`](https://github.com/radxa-pkg/aic8800/tree/d13d07963cd15d731e2895e8288a04cca6152ac9/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv):
`lmac_msg.h`, `lmac_mac.h`, `rwnx_msg_tx.c`, `rwnx_msg_rx.c`, `rwnx_tx.c`,
`rwnx_rx.h` and `aicwf_sdio.c`. The BSD implementation uses those protocol facts; it does
not import the GPL Linux driver. Keep the vendor firmware's redistribution
terms separate from the host driver's licence.
