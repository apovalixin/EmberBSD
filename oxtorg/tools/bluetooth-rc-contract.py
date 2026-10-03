#!/usr/bin/env python3
"""Execute the real rc script with external commands replaced at the boundary."""
import os
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).resolve().parents[1] / "etc/rc.d/oxtorg_bluetooth"
with tempfile.TemporaryDirectory(prefix="bluetooth-rc-") as directory:
    base = Path(directory)
    subr = base / "rc.subr"
    subr.write_text('load_rc_config() { :; }\nrun_rc_command() { "$start_cmd"; }\n'
                    'check_pidfile() { echo 123; }\ncheck_process() { echo 456; }\n')
    stub = base / "stub"
    stub.write_text('''#!/bin/sh
echo "$(basename "$0") $*" >> "$TEST_LOG"
case "$(basename "$0") $*" in
"sysctl -n hw.model") [ "$TEST_CASE" != orangepi ] || echo xunlong,orangepi-zero4 ;;
"sysctl "*) case "$TEST_CASE" in
    foreign) echo QEMU ;; orangepi) : ;; *) echo 'Raspberry Pi 5 Model B' ;; esac ;;
"btconfig -l") case "$TEST_CASE" in cold|orangepi) : ;; *) echo btuart0 ;; esac ;;
"btconfig btuart0") case "$TEST_CASE" in cold|orangepi) : ;; *) echo '<UP,RUNNING>' ;; esac ;;
"modstat "*) : ;;
"bluetooth-control btuart0 address")
    [ "$TEST_CASE" = cold ] && echo ADDRESS_CHANGED || echo ADDRESS_UNCHANGED ;;
"bluetooth-control btuart0 state")
    [ "$TEST_CASE" = cold ] && echo 'SSP=0 SC=0' || echo 'SSP=1 SC=1' ;;
"bluetooth-control btuart0 init "*) [ "$TEST_CASE" != rejected ] ;;
*) : ;;
esac
''')
    stub.chmod(0o755)
    for name in ("sysctl", "btconfig", "modstat", "modload", "btattach", "bthcid", "sdpd", "bluetooth-control"):
        (base / name).symlink_to(stub)
    script = base / "rc"
    script.write_text(source.read_text().replace('/etc/rc.subr', str(subr))
                      .replace('/etc/oxtorg/bluetooth.conf', str(base / 'absent.conf'))
                      .replace('/opt/oxtorg/bin/bluetooth-control', str(base / 'bluetooth-control')))
    failed = False
    for case in ("live", "cold", "orangepi", "foreign", "rejected"):
        log = base / "commands"
        log.write_text("")
        result = subprocess.run(["sh", str(script), "start"], capture_output=True, text=True,
                                env=dict(os.environ, PATH=str(base)+os.pathsep+os.environ['PATH'],
                                         TEST_LOG=str(log), TEST_CASE=case))
        lines = log.read_text().splitlines()
        resets = [line for line in lines if line in ("btconfig btuart0 disable", "btconfig btuart0 enable")]
        good = result.returncode == (1 if case == "rejected" else 0)
        if case in ("live", "rejected"):
            good &= not resets and not any(line.startswith(("modload ", "btattach ")) for line in lines)
        elif case == "cold":
            good &= resets == ["btconfig btuart0 enable", "btconfig btuart0 disable", "btconfig btuart0 enable"]
            good &= lines.index("btconfig btuart0 disable") < lines.index("bluetooth-control btuart0 init Asenta NetBSD")
            good &= any(line.startswith("btattach ") for line in lines)
            good &= "btconfig btuart0 -pscan -iscan" in lines
            configure = "btconfig btuart0 name Asenta NetBSD class 0x000104 pscan iscan -auth -encrypt switch sniff -master"
            good &= lines.index("bluetooth-control btuart0 init Asenta NetBSD") < lines.index(configure)
        elif case == "orangepi":
            good &= "btattach -f btuart /dev/dty01 1500000" in lines
            good &= not any(line.startswith("modload ") for line in lines)
            good &= "btconfig btuart0 enable" in lines
        else:
            good &= len(lines) == 1
        print(case+": "+("PASS" if good else "FAIL"))
        if not good:
            print(log.read_text(), result.stderr)
            failed = True
    raise SystemExit(failed)
