#!/usr/bin/env python3

import audioop
import datetime as dt
import json
import os
import signal
import socket
import subprocess
import threading
import time
import uuid

import numpy as np
import websocket
from whisper_live.server import TranscriptionServer


SAMPLE_RATE = 16000
SAMPLE_WIDTH = 2
CHANNELS = 1
FRAME_SAMPLES = 4096
FRAME_BYTES = FRAME_SAMPLES * SAMPLE_WIDTH * CHANNELS

STATE_PATH = os.getenv("BOOSTER_WHISPERLIVE_ASR_STATE_PATH", "/tmp/booster_whisperlive_asr_state.json")
LOG_PATH = os.getenv("BOOSTER_WHISPERLIVE_ASR_LOG_PATH", "/tmp/booster_whisperlive_asr.log")
MODEL = os.getenv("BOOSTER_WHISPERLIVE_MODEL", "base.en").strip() or "base.en"
LANGUAGE = os.getenv("BOOSTER_WHISPERLIVE_LANGUAGE", "en").strip() or "en"
SERVER_HOST = os.getenv("BOOSTER_WHISPERLIVE_HOST", "127.0.0.1").strip() or "127.0.0.1"
SERVER_PORT = int(os.getenv("BOOSTER_WHISPERLIVE_PORT", "0"))
USE_VAD = os.getenv("BOOSTER_WHISPERLIVE_USE_VAD", "1").strip() not in {"0", "false", "False"}
SOURCE = os.getenv("BOOSTER_WHISPERLIVE_SOURCE", "").strip()
COMMIT_SETTLE_SEC = float(os.getenv("BOOSTER_WHISPERLIVE_COMMIT_SETTLE_SEC", "1.2"))

RUNNING = True
STATE_LOCK = threading.Lock()


def iso_now():
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def write_state(payload):
    payload = dict(payload)
    payload["updated_at"] = iso_now()
    tmp_path = f"{STATE_PATH}.tmp"
    with STATE_LOCK:
        with open(tmp_path, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, ensure_ascii=True, indent=2)
            handle.write("\n")
        os.replace(tmp_path, STATE_PATH)


def set_state_field(state_data, **updates):
    state_data.update(updates)
    write_state(state_data)


def trim(value):
    return str(value or "").strip()


def resolve_source():
    if SOURCE:
        return SOURCE
    try:
        proc = subprocess.run(
            ["pactl", "get-default-source"],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
    except Exception:
        return ""
    if proc.returncode != 0:
        return ""
    return trim(proc.stdout)


def wait_for_port(host, port, timeout_sec):
    deadline = time.time() + timeout_sec
    while time.time() < deadline and RUNNING:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def reserve_server_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        try:
            sock.bind((SERVER_HOST, SERVER_PORT))
        except OSError:
            sock.bind((SERVER_HOST, 0))
        return sock.getsockname()[1]


def handle_signal(_signum, _frame):
    global RUNNING
    RUNNING = False


class WhisperLiveClient:
    def __init__(self, state, port):
        self.state = state
        self.port = port
        self.uid = str(uuid.uuid4())
        self.ws = None
        self.ready = threading.Event()
        self.failed = threading.Event()
        self.error_message = ""
        self.last_segment_key = ""
        self.pending_final = ""
        self.pending_final_at = 0.0
        self.last_committed = ""

    def on_open(self, ws):
        ws.send(json.dumps({
            "uid": self.uid,
            "language": LANGUAGE,
            "task": "transcribe",
            "model": MODEL,
            "use_vad": USE_VAD,
        }))

    def on_message(self, _ws, message):
        try:
            payload = json.loads(message)
        except json.JSONDecodeError:
            return

        if payload.get("uid") != self.uid:
            return

        if payload.get("status") == "ERROR":
            self.error_message = trim(payload.get("message"))
            self.failed.set()
            set_state_field(self.state, ok=False, state="error", running=False, last_error=self.error_message)
            return

        if payload.get("message") == "SERVER_READY":
            set_state_field(
                self.state,
                state="listening",
                backend="whisperlive_asr",
                server_backend=trim(payload.get("backend")),
                last_error="",
            )
            self.ready.set()
            return

        segments = payload.get("segments")
        if not isinstance(segments, list) or not segments:
            return

        latest = segments[-1]
        transcript = trim(latest.get("text"))
        if not transcript:
            return

        segment_key = "|".join([
            trim(latest.get("start")),
            trim(latest.get("end")),
            transcript,
        ])
        is_final = bool(payload.get("is_final"))

        updates = {
            "segment_count": len(segments),
            "last_partial": transcript,
            "state": "listening" if is_final else "hearing",
            "last_error": "",
        }

        if is_final and segment_key != self.last_segment_key:
            self.last_segment_key = segment_key
            self.pending_final = transcript
            self.pending_final_at = time.time()

        set_state_field(self.state, **updates)

    def flush_pending_final(self):
        if not self.pending_final:
            return
        if time.time() - self.pending_final_at < COMMIT_SETTLE_SEC:
            return
        if self.pending_final == self.last_committed:
            self.pending_final = ""
            return
        self.last_committed = self.pending_final
        set_state_field(
            self.state,
            last_heard=self.pending_final,
            state="listening",
            last_error="",
        )
        self.pending_final = ""

    def on_error(self, _ws, error):
        self.error_message = trim(error)
        self.failed.set()
        set_state_field(self.state, ok=False, state="error", running=False, last_error=self.error_message)

    def on_close(self, _ws, _code, _msg):
        if RUNNING and not self.failed.is_set():
            set_state_field(self.state, ok=False, state="error", running=False, last_error="WhisperLive websocket closed")

    def connect(self):
        self.ws = websocket.WebSocketApp(
            f"ws://{SERVER_HOST}:{self.port}",
            on_open=self.on_open,
            on_message=self.on_message,
            on_error=self.on_error,
            on_close=self.on_close,
        )
        thread = threading.Thread(target=self.ws.run_forever, daemon=True)
        thread.start()
        return thread

    def send_audio(self, audio_bytes):
        if self.ws is None:
            return
        self.ws.send(audio_bytes, opcode=websocket.ABNF.OPCODE_BINARY)

    def close(self):
        if self.ws is not None:
            try:
                self.ws.send(b"END_OF_AUDIO", opcode=websocket.ABNF.OPCODE_BINARY)
            except Exception:
                pass
            try:
                self.ws.close()
            except Exception:
                pass


def server_worker(port):
    server = TranscriptionServer()
    server.run(SERVER_HOST, port=port, backend="faster_whisper")


def main():
    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)

    source = resolve_source()
    state = {
        "ok": True,
        "available": True,
        "running": True,
        "pid": os.getpid(),
        "state": "starting",
        "started_at": iso_now(),
        "last_error": "",
        "last_heard": "",
        "last_partial": "",
        "last_spoken": "",
        "last_rms": 0,
        "peak_rms": 0,
        "source": source,
        "log_path": LOG_PATH,
        "backend": "whisperlive_asr",
        "label": "WhisperLive ASR",
        "model": MODEL,
        "language": LANGUAGE,
        "sample_rate_hz": SAMPLE_RATE,
        "server_host": SERVER_HOST,
        "server_port": 0,
        "use_vad": USE_VAD,
    }
    write_state(state)

    port = reserve_server_port()
    state["server_port"] = port
    write_state(state)

    server_thread = threading.Thread(target=server_worker, args=(port,), daemon=True)
    server_thread.start()

    if not wait_for_port(SERVER_HOST, port, 10):
        set_state_field(state, ok=False, running=False, state="error", last_error="WhisperLive server did not start")
        return 1

    client = WhisperLiveClient(state, port)
    ws_thread = client.connect()
    if not client.ready.wait(timeout=180):
        if client.failed.is_set():
            return 1
        set_state_field(state, ok=False, running=False, state="error", last_error="WhisperLive client did not become ready")
        client.close()
        return 1

    command = [
        "parec",
        "--format=s16le",
        f"--rate={SAMPLE_RATE}",
        f"--channels={CHANNELS}",
    ]
    if source:
        command.extend(["--device", source])

    try:
        recorder = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
    except Exception as exc:
        set_state_field(state, ok=False, running=False, state="error", last_error=f"Failed to start parec: {exc}")
        client.close()
        return 1

    try:
        while RUNNING:
            chunk = recorder.stdout.read(FRAME_BYTES)
            if not chunk:
                set_state_field(state, ok=False, running=False, state="error", last_error="Audio capture ended unexpectedly")
                break

            rms = int(audioop.rms(chunk, SAMPLE_WIDTH))
            peak = int(audioop.max(chunk, SAMPLE_WIDTH))
            state["last_rms"] = rms
            state["peak_rms"] = max(int(state.get("peak_rms", 0)), peak)
            write_state(state)

            audio = np.frombuffer(chunk, dtype=np.int16).astype(np.float32) / 32768.0
            client.send_audio(audio.tobytes())

            if client.failed.is_set():
                break
            client.flush_pending_final()
    finally:
        try:
            recorder.terminate()
        except Exception:
            pass
        try:
            recorder.wait(timeout=2)
        except Exception:
            pass
        client.close()
        ws_thread.join(timeout=2)

        if state.get("state") != "error":
            set_state_field(state, running=False, state="stopped")

    return 0 if state.get("ok", False) else 1


if __name__ == "__main__":
    raise SystemExit(main())
