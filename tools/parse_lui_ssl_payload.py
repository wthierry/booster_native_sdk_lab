#!/usr/bin/env python3
from __future__ import annotations

import argparse
import gzip
import json
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


MAGIC = 0x42534C50
HEADER = struct.Struct("<IHHqqqIIIIQ")


@dataclass
class Record:
    sequence: int
    sec: int
    nsec: int
    direction: int
    payload: bytes

    @property
    def direction_name(self) -> str:
        return "out" if self.direction == 0 else "in"

    @property
    def ts(self) -> str:
        return f"{self.sec}.{self.nsec:09d}"


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
        if version != 1:
            raise ValueError(f"unsupported version at offset {offset}: {version}")
        payload_start = offset + header_size
        payload_end = payload_start + payload_len
        if payload_end > len(data):
            raise ValueError(f"truncated payload at offset {offset}")
        yield Record(
            sequence=sequence,
            sec=sec,
            nsec=nsec,
            direction=direction,
            payload=data[payload_start:payload_end],
        )
        offset = payload_end


def printable_ratio(buf: bytes) -> float:
    if not buf:
        return 1.0
    printable = 0
    for b in buf:
        if b in (9, 10, 13) or 32 <= b <= 126:
            printable += 1
    return printable / len(buf)


def looks_like_http(buf: bytes) -> bool:
    return buf.startswith(b"GET ") or buf.startswith(b"HTTP/1.1 ")


def summarize_http(buf: bytes) -> list[str]:
    text = buf.decode("utf-8", "replace")
    lines = [line for line in text.splitlines() if line.strip()]
    return lines[:20]


def try_parse_ws_frame(buf: bytes):
    if len(buf) < 2:
        return None
    b0 = buf[0]
    b1 = buf[1]
    fin = (b0 >> 7) & 1
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
    mask_key = None
    if masked:
      if len(buf) < pos + 4:
        return None
      mask_key = buf[pos:pos + 4]
      pos += 4
    if len(buf) < pos + length:
      return None
    payload = bytearray(buf[pos:pos + length])
    if masked and mask_key:
      for i in range(length):
        payload[i] ^= mask_key[i % 4]
    return {
      "fin": fin,
      "opcode": opcode,
      "masked": masked,
      "length": length,
      "payload": bytes(payload),
      "frame_len": pos + length,
    }


def opcode_name(opcode: int) -> str:
    return {
        0x0: "continuation",
        0x1: "text",
        0x2: "binary",
        0x8: "close",
        0x9: "ping",
        0xA: "pong",
    }.get(opcode, f"opcode_{opcode}")


def summarize_payload(payload: bytes) -> list[str]:
    lines: list[str] = []
    if looks_like_http(payload):
        lines.extend(summarize_http(payload))
        return lines

    frame = try_parse_ws_frame(payload)
    if frame is None:
        ratio = printable_ratio(payload)
        lines.append(f"raw bytes={len(payload)} printable_ratio={ratio:.2f}")
        if ratio > 0.85:
            text = payload.decode("utf-8", "replace").strip()
            if text:
                lines.append(text[:400])
        else:
            lines.append(payload[:32].hex())
        return lines

    op_name = opcode_name(frame["opcode"])
    inner = frame["payload"]
    lines.append(
        f"ws fin={frame['fin']} opcode={op_name} masked={frame['masked']} payload_len={frame['length']}"
    )
    if frame["opcode"] == 0x1:
        text = inner.decode("utf-8", "replace")
        lines.append(text[:400])
        try:
            obj = json.loads(text)
        except Exception:
            pass
        else:
            lines.append("json_keys=" + ",".join(sorted(obj.keys())[:20]))
    elif frame["opcode"] == 0x2:
        ratio = printable_ratio(inner)
        lines.append(f"binary printable_ratio={ratio:.2f}")
        if len(inner) >= 12 and inner[0] == 0x11 and inner[1] == 0x21:
            chunk_seq = int.from_bytes(inner[4:8], "big")
            compressed_len = int.from_bytes(inner[8:12], "big")
            lines.append(
                f"chunk_header type=0x{inner[0]:02x}{inner[1]:02x} chunk_seq={chunk_seq} compressed_len={compressed_len}"
            )
            gzip_offset = inner.find(b"\x1f\x8b\x08")
            if gzip_offset >= 0:
                try:
                    decompressed = gzip.decompress(inner[gzip_offset:])
                except Exception as exc:
                    lines.append(f"gzip_error={exc}")
                else:
                    pcm_samples = len(decompressed) // 2
                    duration_ms = pcm_samples / 16.0
                    lines.append(
                        f"gzip_offset={gzip_offset} pcm_bytes={len(decompressed)} pcm_samples={pcm_samples} duration_ms={duration_ms:.1f}"
                    )
                    lines.append(decompressed[:32].hex())
        if ratio > 0.60:
            snippet = inner.decode("utf-8", "replace").strip()
            if snippet:
                lines.append(snippet[:200])
        else:
            lines.append(inner[:32].hex())
    elif frame["opcode"] in (0x8, 0x9, 0xA):
        lines.append(inner[:64].hex())
    return lines


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("payload_path", type=Path)
    parser.add_argument("--start", type=int, default=1)
    parser.add_argument("--end", type=int)
    parser.add_argument("--limit", type=int, default=200)
    args = parser.parse_args()

    printed = 0
    for record in iter_records(args.payload_path):
        if record.sequence < args.start:
            continue
        if args.end is not None and record.sequence > args.end:
            continue
        print(f"[{record.sequence}] {record.ts} {record.direction_name} bytes={len(record.payload)}")
        for line in summarize_payload(record.payload):
            print(f"  {line}")
        printed += 1
        if printed >= args.limit:
            break
    return 0


if __name__ == "__main__":
    sys.exit(main())
