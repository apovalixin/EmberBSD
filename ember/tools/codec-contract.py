#!/usr/bin/env python3
"""Run the WM8960 format/capability/mixer contract in the NetBSD build guest.

Usage: python3 codec-contract.py /root/build/usr/src
The driver's API functions are compiled into this userspace probe. The I2C
transport is replaced to check the encoded gain and error handling; this
does not verify the kernel ABI, interrupts or actual hardware writes.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def function(source, name):
    pattern = r"static int\n" + name + r"\([^)]*\)\n\{\n.*?^\}\n"
    match = re.search(pattern, source, re.S | re.M)
    if not match:
        raise RuntimeError("codec_function_missing:" + name)
    return match.group()


def main():
    if len(sys.argv) != 2 or not sys.platform.startswith("netbsd"):
        raise SystemExit("usage on NetBSD: codec-contract.py SOURCE_ROOT")
    root = Path(sys.argv[1]) / "sys"
    source = (root / "dev/i2c/rp1wmcodec.c").read_text()
    interface = (root / "dev/audio/audio_if.h").read_text()
    params = re.search(r"typedef struct audio_params \{.*?\} audio_params_t;",
                       interface, re.S)
    if not params:
        raise RuntimeError("codec_audio_params_missing")
    # Filters are unused by these functions; NULL is the contract input.
    program = """
#include <sys/param.h>
#include <sys/audioio.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define WM_MIXER_OUTPUTS 0
#define WM_MIXER_MASTER 1
typedef struct audio_filter_reg audio_filter_reg_t;
/* Only the fields used by the mixer API; replace the I2C transport, not the API. */
typedef void *i2c_tag_t;
typedef unsigned i2c_addr_t;
struct rp1wmcodec_softc {
    i2c_tag_t sc_tag;
    i2c_addr_t sc_addr;
    uint8_t sc_volume[2];
};
static unsigned registers[2];
static int write_error;
static int wm_write(i2c_tag_t tag, i2c_addr_t addr, unsigned reg, unsigned value) {
    assert(addr == 0x1a && (reg == 0x28 || reg == 0x29));
    if (write_error) return write_error;
    registers[reg - 0x28] = value;
    return 0;
}
""" + params.group() + "\n" + function(source, "rp1wmcodec_props") + \
        function(source, "rp1wmcodec_format_set") + \
        function(source, "rp1wmcodec_devinfo") + \
        function(source, "rp1wmcodec_set_port") + \
        function(source, "rp1wmcodec_get_port") + """
int main(void) {
    int required = AUDIO_PROP_CAPTURE | AUDIO_PROP_PLAYBACK | AUDIO_PROP_FULLDUPLEX;
    assert((rp1wmcodec_props(NULL) & required) == required);
    audio_params_t format = { .sample_rate = 48000, .channels = 2,
        .precision = 16, .encoding = AUDIO_ENCODING_SLINEAR_LE };
    assert(rp1wmcodec_format_set(NULL, AUMODE_RECORD | AUMODE_PLAY,
        &format, &format, NULL, NULL) == 0);
    assert(rp1wmcodec_format_set(NULL, AUMODE_RECORD,
        NULL, &format, NULL, NULL) == 0);
    assert(rp1wmcodec_format_set(NULL, AUMODE_PLAY,
        &format, NULL, NULL, NULL) == 0);
    format.sample_rate = 16000;
    assert(rp1wmcodec_format_set(NULL, AUMODE_PLAY,
        &format, NULL, NULL, NULL) == EINVAL);
    assert(rp1wmcodec_format_set(NULL, AUMODE_RECORD,
        NULL, &format, NULL, NULL) == EINVAL);
    format.sample_rate = 48000; format.channels = 1;
    assert(rp1wmcodec_format_set(NULL, AUMODE_PLAY,
        &format, NULL, NULL, NULL) == EINVAL);
    mixer_devinfo_t mixer = { .index = 0 };
    assert(rp1wmcodec_devinfo(NULL, &mixer) == 0);
    assert(mixer.type == AUDIO_MIXER_CLASS);
    assert(strcmp(mixer.label.name, AudioCoutputs) == 0);
    mixer.index = 1;
    assert(rp1wmcodec_devinfo(NULL, &mixer) == 0);
    assert(mixer.type == AUDIO_MIXER_VALUE && mixer.un.v.num_channels == 2);
    assert(mixer.mixer_class == 0);
    assert(strcmp(mixer.label.name, AudioNmaster) == 0);
    mixer.index = 2;
    assert(rp1wmcodec_devinfo(NULL, &mixer) == ENXIO);
    struct rp1wmcodec_softc codec = { .sc_addr = 0x1a };
    mixer_ctrl_t volume = { .dev = 1, .type = AUDIO_MIXER_VALUE };
    volume.un.value.num_channels = 2;
    volume.un.value.level[0] = volume.un.value.level[1] = 255;
    assert(rp1wmcodec_set_port(&codec, &volume) == 0);
    /* Datasheet: 127 is +6 dB; right VU commits both channel latches. */
    assert(registers[0] == 0x7f && registers[1] == 0x17f);
    volume.un.value.level[0] = volume.un.value.level[1] = 0;
    assert(rp1wmcodec_get_port(&codec, &volume) == 0);
    assert(volume.un.value.level[0] == 255 && volume.un.value.level[1] == 255);
    write_error = EIO;
    volume.un.value.level[0] = volume.un.value.level[1] = 0;
    assert(rp1wmcodec_set_port(&codec, &volume) == EIO);
    assert(rp1wmcodec_get_port(&codec, &volume) == 0);
    assert(volume.un.value.level[0] == 255 && volume.un.value.level[1] == 255);
    write_error = 0;
    volume.un.value.level[0] = volume.un.value.level[1] = 0;
    assert(rp1wmcodec_set_port(&codec, &volume) == 0);
    assert(registers[0] == 0 && registers[1] == 0x100);
    volume.un.value.num_channels = 3;
    assert(rp1wmcodec_set_port(&codec, &volume) == EINVAL);
    puts("WM8960 full-duplex and mixer API contract: PASS");
    return 0;
}
"""
    with tempfile.TemporaryDirectory(prefix="ember-codec-contract-") as work:
        src, binary = Path(work) / "contract.c", Path(work) / "contract"
        src.write_text(program)
        subprocess.run([os.environ.get("CC", "cc"), "-O2", "-Wall", "-Werror",
                        "-o", str(binary), str(src)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
