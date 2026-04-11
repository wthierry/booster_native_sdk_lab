#!/usr/bin/env python3

import datetime as dt
import json
import os
import subprocess
import signal
import time
from typing import Optional

from moonshine_voice.download import get_model_for_language
from moonshine_voice.moonshine_api import ModelArch
from moonshine_voice.transcriber import TranscriptEventListener, Transcriber
import numpy as np


STATE_PATH = os.getenv("BOOSTER_MOONSHINE_ASR_STATE_PATH", "/tmp/booster_moonshine_asr_state.json")
LOG_PATH = os.getenv("BOOSTER_MOONSHINE_ASR_LOG_PATH", "/tmp/booster_moonshine_asr.log")
INPUT_WAV_PATH = os.getenv("BOOSTER_MOONSHINE_ASR_INPUT_WAV_PATH", "/tmp/booster_moonshine_asr_input.wav")
LANGUAGE = (os.getenv("BOOSTER_MOONSHINE_ASR_LANGUAGE", "en").strip() or "en").lower()
MODEL_ARCH_NAME = os.getenv("BOOSTER_MOONSHINE_ASR_MODEL", "medium-streaming").strip() or "medium-streaming"
UPDATE_INTERVAL_SEC = float(os.getenv("BOOSTER_MOONSHINE_ASR_UPDATE_INTERVAL_SEC", "0.2"))
DEVICE_NAME = os.getenv("BOOSTER_MOONSHINE_ASR_DEVICE", "").strip()
SAMPLE_RATE = int(os.getenv("BOOSTER_MOONSHINE_ASR_SAMPLE_RATE", "16000"))
BLOCKSIZE = int(os.getenv("BOOSTER_MOONSHINE_ASR_BLOCKSIZE", "1024"))
CHANNELS = int(os.getenv("BOOSTER_MOONSHINE_ASR_CHANNELS", "1"))
PULSE_SOURCE = os.getenv(
    "BOOSTER_MOONSHINE_ASR_SOURCE",
    "alsa_input.usb-iflytek_XFM-DP-V0.0.18_bc00144082144751c10-01.mono-fallback",
).strip()

RUNNING = True

MODEL_ARCH_MAP = {
    "tiny": ModelArch.TINY,
    "base": ModelArch.BASE,
    "tiny-streaming": ModelArch.TINY_STREAMING,
    "base-streaming": ModelArch.BASE_STREAMING,
    "small-streaming": ModelArch.SMALL_STREAMING,
    "medium-streaming": ModelArch.MEDIUM_STREAMING,
}


def iso_now():
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def write_state(payload):
    payload = dict(payload)
    payload["updated_at"] = iso_now()
    tmp_path = f"{STATE_PATH}.tmp"
    with open(tmp_path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=True, indent=2)
        handle.write("\n")
    os.replace(tmp_path, STATE_PATH)


def set_state_field(state_data, **updates):
    state_data.update(updates)
    write_state(state_data)


def trim(value):
    return str(value or "").strip()


def log_line(message: str):
    with open(LOG_PATH, "a", encoding="utf-8") as handle:
        handle.write(f"{iso_now()} {message}\n")


def handle_signal(_signum, _frame):
    global RUNNING
    RUNNING = False


def resolve_model_arch(name: str) -> ModelArch:
    key = trim(name).lower()
    if key in MODEL_ARCH_MAP:
        return MODEL_ARCH_MAP[key]
    return ModelArch.TINY_STREAMING


class StateListener(TranscriptEventListener):
    def __init__(self, state, control):
        self.state = state
        self.control = control
        self.last_partial = ""
        self.last_heard = ""

    def _update_partial(self, text: str, state_name: str):
        text = trim(text)
        if not text:
            return
        self.last_partial = text
        log_line(f"partial: {text}")
        set_state_field(self.state, state=state_name, last_partial=text, last_error="")

    def on_line_started(self, event):
        self._update_partial(event.line.text, "hearing")

    def on_line_updated(self, event):
        self._update_partial(event.line.text, "hearing")

    def on_line_text_changed(self, event):
        self._update_partial(event.line.text, "hearing")

    def on_line_completed(self, event):
        text = trim(event.line.text)
        if not text:
            log_line("completed: <empty>")
            set_state_field(self.state, state="listening", last_error="")
            return
        self.last_heard = text
        log_line(f"heard: {text}")
        set_state_field(
            self.state,
            state="listening",
            last_heard=text,
            last_partial=text,
            last_error="",
        )
        self.control["reset_requested"] = True

    def on_error(self, event):
        log_line(f"error: {trim(event.error)}")
        set_state_field(self.state, ok=False, running=False, state="error", last_error=trim(event.error))


def build_parec_command():
    return [
        "parec",
        "--device",
        PULSE_SOURCE,
        "--format=s16le",
        "--rate",
        str(SAMPLE_RATE),
        "--channels",
        str(CHANNELS),
        "--raw",
    ]


def create_transcriber_stream(model_path, resolved_arch, listener):
    transcriber = Transcriber(
        model_path=model_path,
        model_arch=resolved_arch,
        options={
            "save_input_wav_path": INPUT_WAV_PATH,
            "log_api_calls": False,
        },
    )
    stream = transcriber.create_stream(UPDATE_INTERVAL_SEC)
    stream.add_listener(listener)
    stream.start()
    return transcriber, stream


def close_transcriber_stream(transcriber, stream):
    if stream is not None:
        stream.stop()
        stream.close()
    if transcriber is not None:
        transcriber.close()


def main():
    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)

    model_arch = resolve_model_arch(MODEL_ARCH_NAME)
    open(LOG_PATH, "w", encoding="utf-8").close()

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
        "log_path": LOG_PATH,
        "input_wav_path": INPUT_WAV_PATH,
        "backend": "moonshine_asr",
        "label": "Moonshine ASR",
        "language": LANGUAGE,
        "model": MODEL_ARCH_NAME,
        "device": DEVICE_NAME or "default",
        "sample_rate_hz": SAMPLE_RATE,
        "blocksize": BLOCKSIZE,
        "channels": CHANNELS,
    }
    write_state(state)

    try:
        model_path, resolved_arch = get_model_for_language(LANGUAGE, model_arch)
        state["model_path"] = model_path
        state["model_arch"] = int(resolved_arch)
        write_state(state)
        log_line(
            f"starting model={MODEL_ARCH_NAME} arch={int(resolved_arch)} device={DEVICE_NAME or 'default'} "
            f"samplerate={SAMPLE_RATE} blocksize={BLOCKSIZE} channels={CHANNELS}"
        )

        control = {"reset_requested": False}
        listener = StateListener(state, control)
        transcriber, stream = create_transcriber_stream(model_path, resolved_arch, listener)
        parec_cmd = build_parec_command()
        log_line(f"capture command: {' '.join(parec_cmd)}")
        parec = subprocess.Popen(
            parec_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )
        chunk_bytes = BLOCKSIZE * CHANNELS * 2
        log_line("listening")
        set_state_field(state, state="listening", last_error="")

        while RUNNING:
            if parec.poll() is not None:
                stderr_output = trim((parec.stderr.read() or b"").decode("utf-8", errors="replace"))
                raise RuntimeError(stderr_output or f"parec exited with code {parec.returncode}")
            raw = parec.stdout.read(chunk_bytes)
            if not raw:
                time.sleep(0.01)
                continue
            audio = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
            stream.add_audio(audio, SAMPLE_RATE)
            if control["reset_requested"] and RUNNING:
                control["reset_requested"] = False
                log_line("resetting stream after heard")
                close_transcriber_stream(transcriber, stream)
                transcriber, stream = create_transcriber_stream(model_path, resolved_arch, listener)
                set_state_field(state, state="listening", last_error="")

        parec.terminate()
        try:
            parec.wait(timeout=2)
        except subprocess.TimeoutExpired:
            parec.kill()
        close_transcriber_stream(transcriber, stream)
        log_line("stopped")
        set_state_field(state, running=False, state="stopped", last_error="")
        return 0
    except Exception as exc:
        log_line(f"fatal: {trim(exc)}")
        set_state_field(state, ok=False, running=False, state="error", last_error=trim(exc))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
