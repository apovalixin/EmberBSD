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
this path yet. Firmware PMF behavior needs separate physical acceptance;
passing the software checks alone is not a WPA3 support claim.

SAE configuration is privileged. A generation and peer bind commands to one
association. Authentication frames must have bounded length, the selected
AP's addresses and SAE algorithm; fragments, foreign peers and other frame
types are rejected. Firmware response status must be present. External-auth requests accept
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

## Checks

```sh
sh ember/tests/aicwf-sae.sh
sh ember/tests/aicwf-scan.sh
sh ember/tests/aicwf-command.sh
```

The first runs bounded AIC request/frame/IGTK checks and the shared RSN
security checks also used by bwfm. For a live ABI check, build
`ember/tools/bwfm-sae-ioctl.c` with the current net80211 headers and run it as
root with `aicwf0`, with the supplicant stopped and recovery arranged.
It checks lengths, version/reserved fields, stale authorization and denial
of unprivileged configuration. Its historical filename does not restrict
the driver being tested.

## Protocol provenance

The wire layout was inspected in Radxa's AIC8800 SDIO driver at revision
[`d13d07963cd15d731e2895e8288a04cca6152ac9`](https://github.com/radxa-pkg/aic8800/tree/d13d07963cd15d731e2895e8288a04cca6152ac9/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv):
`lmac_msg.h`, `lmac_mac.h`, `rwnx_msg_tx.c`, `rwnx_msg_rx.c`, `rwnx_tx.c`
and `rwnx_rx.h`. The BSD implementation uses those protocol facts; it does
not import the GPL Linux driver. Keep the vendor firmware's redistribution
terms separate from the host driver's licence.
