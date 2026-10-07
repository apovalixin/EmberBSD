# Raspberry Pi 5 power-button fallback

This optional module preserves the GPIO20 power-button path on existing
Pi 5 installations whose UEFI does not deliver ACPI button events. It reads
the input, debounces it and sends a standard `sysmon_power` event. It does
not configure GPIO pins or claim their interrupts. The firmware must have
already configured GPIO20 as an input.

The source was recovered from a deployed EmberBSD Pi 5 on 2026-10-07.
The original source has SHA256
`23c35c01ce8b9bfb409971aeaa600d8b1fde5242cd65750a4bf976b508c7ed98`.
The original BSD-2-Clause identifier and implementation are preserved;
the import adds an origin comment and uses the common module build rules.
No upstream acceptance or new authorship is claimed.

`ember/build-kernel.sh` builds this module with the kernel and the other
board modules. Deploy only the module from the matching kernel build.
Loading remains opt-in: preserve an existing installation's `rpi5button`
rc service, or load it with `modload rpi5button` after starting `powerd`.
Do not enable it alongside a working ACPI power-button event source.

A successful build or module load does not prove that pressing the button
shuts down the board. A physical press and subsequent power-on must be
checked separately when changing firmware or the event path.
