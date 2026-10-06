#!/usr/bin/env python3
"""Check RTP sample clocks from the real bta2dpd encoder, without Bluetooth."""
import struct
import subprocess
import sys

# At bitpool 8: 28-byte stereo SBC frames, 13 frames in each RTP packet.
pcm = bytes(128 * 13 * 4 * 4)
result = subprocess.run([sys.argv[1], "-t", "-B", "8"], input=pcm,
                        capture_output=True, timeout=10)
packets = []
offset = 0
while offset < len(result.stdout):
    header = result.stdout[offset:offset + 13]
    assert len(header) == 13 and header[:2] == b"\x80\x60", "invalid RTP header"
    frames = header[12] & 15
    seq, timestamp = struct.unpack_from(">HI", header, 2)
    length = 13 + frames * 28
    packet = result.stdout[offset:offset + length]
    assert len(packet) == length and packet[13] == 0x9c, "truncated SBC payload"
    packets.append((seq, timestamp, frames))
    offset += length
assert len(packets) >= 3, (result.returncode, result.stderr.decode(errors="replace"))
for previous, current in zip(packets, packets[1:]):
    assert current[0] == (previous[0] + 1) % 65536, "RTP sequence discontinuity"
    expected = 128 * previous[2]
    actual = (current[1] - previous[1]) % (1 << 32)
    assert actual == expected, f"RTP clock advances {actual}, expected {expected} audio samples"
print(f"PASS: {len(packets)} native RTP packets, timestamps count samples per channel")
