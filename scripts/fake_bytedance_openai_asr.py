#!/usr/bin/env python3

import argparse
import asyncio
import audioop
import datetime as dt
import gzip
import json
import os
import signal
import ssl
import subprocess
import tempfile
import time
import uuid
import wave
from pathlib import Path

import websockets


MODEL = "gpt-4o-mini-transcribe"
LANGUAGE = "en"
PROMPT = "Transcribe spoken English from a robot microphone."
API_URL = "https://api.openai.com/v1/audio/transcriptions"


def iso_now():
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def trim(value):
    return str(value or "").strip()


def parse_env_file(path):
    if not path.exists():
        return
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        value = value.strip().strip('"').strip("'")
        os.environ.setdefault(key.strip(), value)


def load_env():
    parse_env_file(Path("/home/booster/Workspace/booster_native_sdk_lab/.env"))
    parse_env_file(Path("/home/booster/Workspace/booster_native_sdk_lab/.env.openai_proxy"))


def build_arg_parser():
    parser = argparse.ArgumentParser(description="Fake ByteDance ASR server backed by OpenAI transcription")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=443)
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--log-path", default="/var/log/fake_bytedance_openai_asr.log")
    parser.add_argument("--pid-path", default="/run/fake_bytedance_openai_asr.pid")
    parser.add_argument("--work-dir", default="/tmp/fake_bytedance_openai_asr")
    return parser


class Session:
    def __init__(self, args):
        self.args = args
        self.log_id = dt.datetime.now().strftime("%Y%m%d%H%M%S") + uuid.uuid4().hex[:16].upper()
        self.result_seq = 1
        self.audio_seq = 0
        self.sample_rate = 16000
        self.channels = 1
        self.bits = 16
        self.audio_started = False
        self.log_id_sent = False
        self.total_samples = 0
        self.buffer = bytearray()
        self.segment = bytearray()
        self.segment_start_sample = None
        self.pending_above = 0
        self.silence_frames = 0
        self.segment_count = 0
        self.log_path = Path(args.log_path)
        self.work_dir = Path(args.work_dir)
        self.work_dir.mkdir(parents=True, exist_ok=True)
        self.start_threshold = int(os.getenv("BOOSTER_FAKE_ASR_START_THRESHOLD", "1200"))
        self.continue_threshold = int(os.getenv("BOOSTER_FAKE_ASR_CONTINUE_THRESHOLD", "500"))
        self.start_frames = int(os.getenv("BOOSTER_FAKE_ASR_START_FRAMES", "2"))
        self.silence_hold_frames = int(os.getenv("BOOSTER_FAKE_ASR_SILENCE_FRAMES", "8"))
        self.max_segment_frames = int(os.getenv("BOOSTER_FAKE_ASR_MAX_SEGMENT_FRAMES", "140"))
        self.prefix_frames = int(os.getenv("BOOSTER_FAKE_ASR_PREFIX_FRAMES", "4"))
        self.min_segment_frames = int(os.getenv("BOOSTER_FAKE_ASR_MIN_SEGMENT_FRAMES", "4"))
        self.preroll = []
        self.speaking = False
        self.last_text = ""

    def log(self, message):
        with self.log_path.open("a", encoding="utf-8") as handle:
            handle.write(f"{iso_now()} {message}\n")

    def parse_config(self, payload):
        if len(payload) < 8:
            return
        if payload[:4] != b"\x11\x10\x10\x00":
            return
        try:
            body = json.loads(payload[8:].decode("utf-8"))
        except Exception:
            self.log("failed to parse config frame")
            return
        audio = body.get("audio", {})
        self.sample_rate = int(audio.get("rate", self.sample_rate))
        self.channels = int(audio.get("channel", self.channels))
        self.bits = int(audio.get("bits", self.bits))
        self.log(f"config rate={self.sample_rate} channel={self.channels} bits={self.bits}")

    def decode_audio_chunk(self, payload):
        if len(payload) < 15 or payload[:2] != b"\x11\x21":
            return None
        chunk_seq = int.from_bytes(payload[4:8], "big")
        compressed_len = int.from_bytes(payload[8:12], "big")
        gzip_offset = payload.find(b"\x1f\x8b\x08")
        if gzip_offset < 0:
            return None
        compressed = payload[gzip_offset:gzip_offset + compressed_len]
        try:
            pcm = gzip.decompress(compressed)
        except Exception as exc:
            self.log(f"gzip decode failed seq={chunk_seq}: {trim(exc)}")
            return None
        return chunk_seq, pcm

    async def send_log_id(self, websocket):
        if self.log_id_sent:
            return
        body = json.dumps(
            {"result": {"additions": {"log_id": self.log_id}}},
            ensure_ascii=False,
            separators=(",", ":"),
        ).encode("utf-8")
        frame = b"\x11\x90\x10\x00" + len(body).to_bytes(4, "big") + body
        await websocket.send(frame)
        self.log_id_sent = True
        self.log("sent log_id frame")

    def build_result_frame(self, text, start_sample, end_sample, definite=True):
        start_ms = int(start_sample * 1000 / self.sample_rate)
        end_ms = int(end_sample * 1000 / self.sample_rate)
        duration_ms = int(self.total_samples * 1000 / self.sample_rate)
        utterance = {
            "additions": {"fixed_prefix_result": ""},
            "definite": definite,
            "start_time": start_ms,
            "end_time": end_ms,
            "text": text,
            "words": self.build_words(text, start_ms, end_ms),
        }
        result = {
            "audio_info": {"duration": duration_ms},
            "result": {
                "additions": {"log_id": self.log_id},
                "last_stream_duration": duration_ms,
                "text": text,
                "utterances": [utterance],
            },
        }
        body = json.dumps(result, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        frame = (
            b"\x11\x91\x10\x00"
            + self.result_seq.to_bytes(4, "big")
            + len(body).to_bytes(4, "big")
            + body
        )
        self.result_seq += 1
        return frame

    def build_words(self, text, start_ms, end_ms):
        words = [word for word in text.strip().split() if word]
        if not words:
            return []
        span = max(end_ms - start_ms, len(words))
        step = max(1, span // len(words))
        entries = []
        current = start_ms
        for index, word in enumerate(words):
            word_end = end_ms if index == len(words) - 1 else min(end_ms, current + step)
            entries.append({"start_time": current, "end_time": word_end, "text": word})
            current = word_end
        return entries

    def append_preroll(self, pcm):
        self.preroll.append(pcm)
        if len(self.preroll) > self.prefix_frames:
            self.preroll.pop(0)

    async def handle_audio_chunk(self, websocket, chunk_seq, pcm):
        if not self.audio_started:
            self.audio_started = True
            await self.send_log_id(websocket)
        rms = int(audioop.rms(pcm, 2))
        samples = len(pcm) // 2
        current_end = self.total_samples + samples
        self.append_preroll(pcm)

        if not self.speaking:
            if rms >= self.start_threshold:
                self.pending_above += 1
            else:
                self.pending_above = 0
            if self.pending_above >= self.start_frames:
                self.speaking = True
                self.silence_frames = 0
                self.segment = bytearray(b"".join(self.preroll))
                self.segment_start_sample = max(0, self.total_samples - (len(self.preroll) * samples))
                self.log(f"speech_start chunk_seq={chunk_seq} rms={rms}")
        else:
            self.segment.extend(pcm)
            if rms >= self.continue_threshold:
                self.silence_frames = 0
            else:
                self.silence_frames += 1
            segment_frames = len(self.segment) // len(pcm) if len(pcm) else 0
            if self.silence_frames >= self.silence_hold_frames or segment_frames >= self.max_segment_frames:
                if segment_frames >= self.min_segment_frames:
                    await self.flush_segment(websocket, current_end)
                self.speaking = False
                self.pending_above = 0
                self.silence_frames = 0
                self.segment = bytearray()
                self.segment_start_sample = None

        self.total_samples = current_end

    async def flush_segment(self, websocket, end_sample):
        if not self.segment:
            return
        start_sample = self.segment_start_sample or max(0, end_sample - (len(self.segment) // 2))
        self.segment_count += 1
        wav_path = self.work_dir / f"segment_{self.segment_count:03d}.wav"
        with wave.open(str(wav_path), "wb") as handle:
            handle.setnchannels(self.channels)
            handle.setsampwidth(2)
            handle.setframerate(self.sample_rate)
            handle.writeframes(bytes(self.segment))
        text = transcribe_wav(wav_path)
        self.log(
            f"segment_done idx={self.segment_count} start_ms={int(start_sample * 1000 / self.sample_rate)} "
            f"end_ms={int(end_sample * 1000 / self.sample_rate)} text={json.dumps(text)}"
        )
        if not text:
            return
        self.last_text = text
        frame = self.build_result_frame(text, start_sample, end_sample, definite=True)
        await websocket.send(frame)
        self.log(f"sent_result seq={self.result_seq - 1} text={json.dumps(text)}")

    async def flush_pending(self, websocket):
        if self.speaking and self.segment:
            await self.flush_segment(websocket, self.total_samples)
            self.speaking = False
            self.segment = bytearray()
            self.segment_start_sample = None


def transcribe_wav(path):
    api_key = (
        trim(os.getenv("OPENAI_API_KEY"))
        or trim(os.getenv("CHATGPT_API_KEY"))
        or trim(os.getenv("CHAT_GPT_API"))
    )
    if not api_key:
        return ""
    cmd = [
        "curl",
        "--silent",
        "--show-error",
        "--fail-with-body",
        API_URL,
        "-H",
        f"Authorization: Bearer {api_key}",
        "-F",
        f"model={MODEL}",
        "-F",
        f"file=@{path};type=audio/wav",
        "-F",
        f"language={LANGUAGE}",
        "-F",
        f"prompt={PROMPT}",
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120, check=False)
    if proc.returncode != 0:
        raise RuntimeError(trim(proc.stderr) or trim(proc.stdout) or f"curl exited {proc.returncode}")
    payload = json.loads(proc.stdout)
    return trim(payload.get("text"))


async def handle_connection(websocket):
    session = Session(handle_connection.args)
    session.log("connection_open")
    try:
        await session.send_log_id(websocket)
        async for message in websocket:
            if not isinstance(message, bytes):
                session.log(f"unexpected_text_message={json.dumps(message)}")
                continue
            if len(message) >= 8 and message[:4] == b"\x11\x10\x10\x00":
                session.parse_config(message)
                continue
            decoded = session.decode_audio_chunk(message)
            if decoded is None:
                session.log(f"ignored_binary len={len(message)} head={message[:16].hex()}")
                continue
            chunk_seq, pcm = decoded
            await session.handle_audio_chunk(websocket, chunk_seq, pcm)
    except websockets.ConnectionClosed:
        session.log("connection_closed")
    except Exception as exc:
        session.log(f"connection_error={trim(exc)}")
    finally:
        try:
            await session.flush_pending(websocket)
        except Exception as exc:
            session.log(f"flush_error={trim(exc)}")


async def main_async(args):
    load_env()
    ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ssl_context.load_cert_chain(args.cert, args.key)
    handle_connection.args = args
    stop_event = asyncio.Event()

    def request_stop():
        stop_event.set()

    loop = asyncio.get_running_loop()
    for signum in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(signum, request_stop)

    with open(args.pid_path, "w", encoding="utf-8") as handle:
        handle.write(f"{os.getpid()}\n")

    Path(args.log_path).parent.mkdir(parents=True, exist_ok=True)
    Path(args.log_path).touch()

    async with websockets.serve(
        handle_connection,
        args.host,
        args.port,
        ssl=ssl_context,
        max_size=None,
        ping_interval=20,
        ping_timeout=20,
    ):
        await stop_event.wait()


def main():
    args = build_arg_parser().parse_args()
    try:
        return asyncio.run(main_async(args))
    finally:
        try:
            os.unlink(args.pid_path)
        except OSError:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
