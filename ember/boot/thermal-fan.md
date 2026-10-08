# Keep an ACPI fan at maximum cooling

Some four-wire fans fitted to a CM5 carrier stall or repeatedly restart at
low PWM duty. The thermal-zone driver provides an explicit maximum-cooling
setting for this case. Automatic temperature control remains the default.

This control is available only for an ACPI thermal zone that supplies its
maximum active-cooling list (`_AL0`). Check the thermal-zone and fan names
on the actual board; numbering and wiring are firmware-specific.

```sh
sysctl hw.acpitz0.force_fan
sysctl -w hw.acpitz0.force_fan=1
envstat -d acpitz0
```

A value of `1` keeps the zone's maximum active cooling enabled even below
its usual temperature threshold. Values other than `0` and `1` are rejected.
Temperature monitoring and hot/critical reporting remain enabled. The
request is applied through the ACPI worker, including after the driver
refreshes the cooling-zone description. It does not change the firmware's
PWM frequency or redefine the speed of the maximum level.

To keep this policy after reboot, add one line to `/etc/sysctl.conf`:

```conf
hw.acpitz0.force_fan=1
```

To restore automatic operation, remove that persistent setting and run:

```sh
sysctl -w hw.acpitz0.force_fan=0
```

Check actual continuous rotation, temperature and undervoltage indicators.
An ACPI `D0` state confirms the requested power state, not fan RPM.
On the CM5 UEFI used in the hardware check, `_AL0` drives `acpifan0` at
250/255 PWM. Other carriers, fans and firmware require their own check.
This setting does not establish that intermediate PWM levels work.

## Validation

On 2026-10-08 a CM5 on Waveshare CM5-NANO-B booted revision
`72c00a3bbc19` with `force_fan=1` applied from sysctl.conf. Its operator
confirmed continuous rotation. Values -1 and 2 were rejected; switching
0/1 returned acpifan0 to D3/D0. Nineteen samples over 18 minutes retained
D0 at 37.5–41.3 °C, including a zone-refresh interval. The setting also
returned after booting the later `b78f10c721f7` kernel. Matching modules were
cross-built on macOS.

The notification regression executes the actual zone-refresh callback with
controlled ACPI providers; it failed before the callback reapplied the policy:

```sh
sh ember/tests/acpitz-fan.sh
```

This contract passes on macOS and an EmberBSD AArch64 VM. It does not
measure fan RPM or replace a physical check. Suspend/resume, other fans and
long-duration operation have not been tested.
