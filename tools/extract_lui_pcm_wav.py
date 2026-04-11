#!/usr/bin/env python3
from __future__ import annotations

import argparse
import gzip
import json
import re
import struct
import sys
import wave
from pathlib import Path

MAGIC = 0x42534C50
HEADER = struct.Struct("<IHHqqqIIIIQ")


def iter_records(path: Path):
    data = path.read_bytes()
    offset = 0
    while offset + HEADER.size <= len(data):
        (
            magic,
            version,
            header_size,
            sequence,
            sec,
            nsec,
            pid,
            tid,
            direction,
            reserved,
            payload_len,
        ) = HEADER.unpack_from(data, offset)
        if magic != MAGIC:
            raise ValueError(f"bad magic at offset {offset}: {magic:#x}")
        payload_start = offset + header_size
        payload_end = payload_start + payload_len
        if payload_end > len(data):
            raise ValueError(f"truncated payload at offset {offset}")
        yield sequence, direction, data[payload_start:payload_end]
        offset = payload_end


def parse_ws_frame(buf: bytes):
    if len(buf) < 2:
        return None
    b0 = buf[0]
    b1 = buf[1]
    opcode = b0 & 0x0F
    masked = (b1 >> 7) & 1
    length = b1 & 0x7F
    pos = 2
    if length == 126:
        if len(buf) < pos + 2:
            return None
        length = struct.unpack_from("!H", buf, pos)[0]
        pos += 2
    elif length == 127:
        if len(buf) < pos + 8:
            return None
        length = struct.unpack_from("!Q", buf, pos)[0]
        pos += 8
    mask_key = b""
    if masked:
        if len(buf) < pos + 4:
            return None
        mask_key = buf[pos : pos + 4]
        pos += 4
    if len(buf) < pos + length:
        return None
    payload = bytearray(buf[pos : pos + length])
    if masked:
        for i in range(length):
            payload[i] ^= mask_key[i % 4]
    return opcode, bytes(payload)


def infer_sample_rate(records: list[tuple[int, int, bytes]]) -> int:
    for _, _, payload in records:
        frame = parse_ws_frame(payload)
        if not frame:
            continue
        opcode, inner = frame
        if opcode != 0x2:
            continue
        if b'"audio"' not in inner or b'"rate"' not in inner:
            continue
        text = inner.decode("utf-8", "replace")
        match = re.search(r'"rate"\s*:\s*(\d+)', text)
        if match:
            return int(match.group(1))
    return 16000


def extract_pcm_chunks(records: list[tuple[int, int, bytes]]):
    chunks = []
    for seq, direction, payload in records:
        if direction != 0:
            continue
        frame = parse_ws_frame(payload)
        if not frame:
            continue
        opcode, inner = frame
        if opcode != 0x2:
            continue
        if len(inner) < 15:
            continue
        if inner[:2] != b"\x11\x21":
            continue
        gzip_offset = inner.find(b"\x1f\x8b\x08")
        if gzip_offset < 0:
            continue
        try:
            pcm = gzip.decompress(inner[gzip_offset:])
        except Exception:
            continue
        chunk_seq = int.from_bytes(inner[4:8], "big")
        chunks.append((seq, chunk_seq, pcm))
    chunks.sort(key=lambda item: item[1])
    return chunks


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("payload_path", type=Path)
    parser.add_argument("wav_path", type=Path)
    args = parser.parse_args()

    records = list(iter_records(args.payload_path))
    if not records:
      raise SystemExit("no records found")

    sample_rate = infer_sample_rate(records)
    chunks = extract_pcm_chunks(records)
    if not chunks:
        raise SystemExit("no PCM chunks found")

    pcm_data = b"".join(chunk for _, _, chunk in chunks)
    with wave.open(str(args.wav_path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(pcm_data)

    duration = len(pcm_data) / 2 / sample_rate
    print(
        json.dumps(
            {
                "wav_path": str(args.wav_path),
                "sample_rate": sample_rate,
                "chunk_count": len(chunks),
                "pcm_bytes": len(pcm_data),
                "duration_sec": round(duration, 3),
                "first_chunk_seq": chunks[0][1],
                "last_chunk_seq": chunks[-1][1],
            }
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
