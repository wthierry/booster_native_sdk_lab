#!/usr/bin/env python3

import datetime as dt
import json
import os
import platform
import subprocess
import signal
import time

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
IS_MAC = platform.system() == "Darwin"

if IS_MAC:
    import sounddevice as sd

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
    def __init__(self, state):
        self.state = state
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
            last_partial="",
            last_error="",
        )

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


def resolve_sounddevice_input_device():
    configured = trim(DEVICE_NAME)
    if not configured or configured.lower() == "default":
        return None
    if configured.isdigit():
        return int(configured)
    devices = sd.query_devices()
    lowered = configured.lower()
    for index, device in enumerate(devices):
        if device.get("max_input_channels", 0) <= 0:
            continue
        if lowered in str(device.get("name", "")).lower():
            return index
    return configured


def describe_sounddevice_input_device(device):
    if device is None:
        default_input = sd.default.device[0]
        if default_input is None or default_input < 0:
            return "default"
        try:
            return f"{default_input}:{sd.query_devices(default_input)['name']}"
        except Exception:
            return "default"
    try:
        details = sd.query_devices(device)
        return f"{device}:{details['name']}"
    except Exception:
        return str(device)


class SoundDeviceCapture:
    def __init__(self, stream, state):
        self.stream = stream
        self.state = state
        self.device = resolve_sounddevice_input_device()
        self.selected_device = describe_sounddevice_input_device(self.device)
        self.input_stream = None

    def start(self):
        def audio_callback(indata, frames, callback_time, status):
            del frames, callback_time
            if not RUNNING:
                return
            if status:
                log_line(f"capture status: {trim(status)}")
            audio = np.asarray(indata, dtype=np.float32).reshape(-1)
            self.stream.add_audio(audio, SAMPLE_RATE)

        self.input_stream = sd.InputStream(
            samplerate=SAMPLE_RATE,
            blocksize=BLOCKSIZE,
            device=self.device,
            channels=CHANNELS,
            dtype="float32",
            callback=audio_callback,
        )
        self.input_stream.start()
        set_state_field(self.state, source=self.selected_device)
        log_line(f"capture backend: sounddevice device={self.selected_device}")

    def stop(self):
        if self.input_stream is None:
            return
        self.input_stream.stop()
        self.input_stream.close()
        self.input_stream = None


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

        listener = StateListener(state)
        transcriber, stream = create_transcriber_stream(model_path, resolved_arch, listener)
        capture = None
        parec = None
        chunk_bytes = BLOCKSIZE * CHANNELS * 2
        if IS_MAC:
            capture = SoundDeviceCapture(stream, state)
            capture.start()
        else:
            parec_cmd = build_parec_command()
            log_line(f"capture command: {' '.join(parec_cmd)}")
            parec = subprocess.Popen(
                parec_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                bufsize=0,
            )
        log_line("listening")
        set_state_field(state, state="listening", last_error="")

        while RUNNING:
            if IS_MAC:
                time.sleep(0.1)
                continue
            if parec.poll() is not None:
                stderr_output = trim((parec.stderr.read() or b"").decode("utf-8", errors="replace"))
                raise RuntimeError(stderr_output or f"parec exited with code {parec.returncode}")
            raw = parec.stdout.read(chunk_bytes)
            if not raw:
                time.sleep(0.01)
                continue
            audio = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
            stream.add_audio(audio, SAMPLE_RATE)

        if capture is not None:
            capture.stop()
        if parec is not None:
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
