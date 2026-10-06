#!/usr/bin/env python3
"""Export the already-patched NetBSD A2DP encoder from this immutable tree."""
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
FILES = ("bta2dpd/bta2dpd.c", "bta2dpd/avdtp.c", "bta2dpd/sbc_encode.c",
         "bta2dpd/sbc_encode.h", "bta2dpd/avdtp_signal.h", "cosdata-gen/cosdata.c",
         "sbc_crc-gen/sbc_crc.c")


def fetch(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    notices = []
    for name in FILES:
        source = ROOT / "usr.sbin/bta2dpd" / name
        target = directory / Path(name).name
        with target.open("xb") as output, source.open("rb") as input:
            shutil.copyfileobj(input, output)
        for notice in re.findall(r"/\*-.*?\*/", source.read_text(), re.DOTALL):
            if "Copyright" in notice and notice not in notices:
                notices.append(notice)
    (directory / "LICENSE").write_text("\n\n".join(notices) + "\n")


if __name__ == "__main__":
    fetch(sys.argv[1])
