#!/usr/bin/env python3

import audioop
import datetime as dt
import json
import os
import signal
import subprocess
import tempfile
import time
import wave


SAMPLE_RATE = int(os.getenv("BOOSTER_OPENAI_ASR_SAMPLE_RATE", "16000"))
SAMPLE_WIDTH = 2
CHANNELS = int(os.getenv("BOOSTER_OPENAI_ASR_CHANNELS", "1"))
FRAME_SAMPLES = int(os.getenv("BOOSTER_OPENAI_ASR_FRAME_SAMPLES", "1024"))
FRAME_BYTES = FRAME_SAMPLES * SAMPLE_WIDTH * CHANNELS
STATE_PATH = os.getenv("BOOSTER_OPENAI_ASR_STATE_PATH", "/tmp/booster_openai_asr_state.json")
LOG_PATH = os.getenv("BOOSTER_OPENAI_ASR_LOG_PATH", "/tmp/booster_openai_asr.log")
MODEL = (os.getenv("BOOSTER_OPENAI_ASR_MODEL", "gpt-4o-mini-transcribe").strip()
         or "gpt-4o-mini-transcribe")
LANGUAGE = os.getenv("BOOSTER_OPENAI_ASR_LANGUAGE", "").strip()
PROMPT = os.getenv("BOOSTER_OPENAI_ASR_PROMPT", "").strip()
API_URL = (os.getenv("BOOSTER_OPENAI_ASR_URL", "https://api.openai.com/v1/audio/transcriptions").strip()
           or "https://api.openai.com/v1/audio/transcriptions")
SOURCE = os.getenv("BOOSTER_OPENAI_ASR_SOURCE", "").strip()
API_KEY = (os.getenv("OPENAI_API_KEY", "").strip()
           or os.getenv("CHATGPT_API_KEY", "").strip()
           or os.getenv("CHAT_GPT_API", "").strip())
MIN_SPEECH_SEC = float(os.getenv("BOOSTER_OPENAI_ASR_MIN_SPEECH_SEC", "0.35"))
MAX_SEGMENT_SEC = float(os.getenv("BOOSTER_OPENAI_ASR_MAX_SEGMENT_SEC", "8.0"))
SILENCE_HOLD_SEC = float(os.getenv("BOOSTER_OPENAI_ASR_SILENCE_HOLD_SEC", "0.8"))
RMS_THRESHOLD = int(os.getenv("BOOSTER_OPENAI_ASR_RMS_THRESHOLD", "1100"))
PREROLL_SEC = float(os.getenv("BOOSTER_OPENAI_ASR_PREROLL_SEC", "0.25"))
TEMPERATURE = os.getenv("BOOSTER_OPENAI_ASR_TEMPERATURE", "").strip()
START_THRESHOLD = int(os.getenv("BOOSTER_OPENAI_ASR_START_THRESHOLD", str(RMS_THRESHOLD)))
CONTINUE_THRESHOLD = int(os.getenv("BOOSTER_OPENAI_ASR_CONTINUE_THRESHOLD", str(max(1, RMS_THRESHOLD // 2))))
SILENCE_FRAMES = int(os.getenv("BOOSTER_OPENAI_ASR_SILENCE_FRAMES", "0"))
MIN_VOICED_FRAMES = int(os.getenv("BOOSTER_OPENAI_ASR_MIN_VOICED_FRAMES", "0"))
PREFIX_FRAMES = int(os.getenv("BOOSTER_OPENAI_ASR_PREFIX_FRAMES", "0"))
MAX_FRAMES = int(os.getenv("BOOSTER_OPENAI_ASR_MAX_FRAMES", "0"))

RUNNING = True


def iso_now():
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def trim(value):
    return str(value or "").strip()


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


def log_line(message):
    with open(LOG_PATH, "a", encoding="utf-8") as handle:
        handle.write(f"{iso_now()} {message}\n")


def handle_signal(_signum, _frame):
    global RUNNING
    RUNNING = False


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


def build_parec_command(source):
    command = [
        "parec",
        "--format=s16le",
        f"--rate={SAMPLE_RATE}",
        f"--channels={CHANNELS}",
        "--raw",
    ]
    if source:
        command.extend(["--device", source])
    return command


def write_wav(path, frames):
    with wave.open(path, "wb") as output:
        output.setnchannels(CHANNELS)
        output.setsampwidth(SAMPLE_WIDTH)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(b"".join(frames))


def transcribe_file(path):
    command = [
        "curl",
        "--silent",
        "--show-error",
        "--fail-with-body",
        API_URL,
        "-H",
        f"Authorization: Bearer {API_KEY}",
        "-F",
        f"model={MODEL}",
        "-F",
        f"file=@{path};type=audio/wav",
    ]
    if LANGUAGE:
        command.extend(["-F", f"language={LANGUAGE}"])
    if PROMPT:
        command.extend(["-F", f"prompt={PROMPT}"])
    if TEMPERATURE:
        command.extend(["-F", f"temperature={TEMPERATURE}"])

    proc = subprocess.run(
        command,
        capture_output=True,
        text=True,
        timeout=90,
        check=False,
    )
    if proc.returncode != 0:
        message = trim(proc.stderr) or trim(proc.stdout) or f"curl exited with {proc.returncode}"
        raise RuntimeError(message)

    payload = json.loads(proc.stdout)
    text = trim(payload.get("text"))
    if not text:
        raise RuntimeError("OpenAI transcription response did not contain text")
    return text


def main():
    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)

    source = resolve_source()
    open(LOG_PATH, "w", encoding="utf-8").close()

    state = {
        "ok": True,
        "available": bool(API_KEY),
        "running": True,
        "pid": os.getpid(),
        "state": "starting",
        "started_at": iso_now(),
        "last_error": "",
        "last_heard": "",
        "last_partial": "",
        "last_spoken": "",
        "backend": "openai_asr",
        "label": "OpenAI ASR",
        "log_path": LOG_PATH,
        "model": MODEL,
        "language": LANGUAGE,
        "source": source,
        "sample_rate_hz": SAMPLE_RATE,
        "frame_samples": FRAME_SAMPLES,
        "rms_threshold": RMS_THRESHOLD,
        "start_threshold": START_THRESHOLD,
        "continue_threshold": CONTINUE_THRESHOLD,
        "api_url": API_URL,
    }
    write_state(state)

    if not API_KEY:
        log_line("fatal: OPENAI_API_KEY or CHATGPT_API_KEY is not set")
        set_state_field(state, ok=False, running=False, state="error", last_error="missing OpenAI API key")
        return 1

    parec_cmd = build_parec_command(source)
    log_line(f"capture command: {' '.join(parec_cmd)}")
    recorder = None
    try:
        recorder = subprocess.Popen(
            parec_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )
    except Exception as exc:
        log_line(f"fatal: failed to start parec: {trim(exc)}")
        set_state_field(state, ok=False, running=False, state="error", last_error=f"failed to start parec: {trim(exc)}")
        return 1

    min_speech_bytes = int(MIN_SPEECH_SEC * SAMPLE_RATE * SAMPLE_WIDTH * CHANNELS)
    max_segment_bytes = int(MAX_SEGMENT_SEC * SAMPLE_RATE * SAMPLE_WIDTH * CHANNELS)
    silence_hold_frames = max(1, int((SILENCE_HOLD_SEC * SAMPLE_RATE) / FRAME_SAMPLES))
    preroll_limit = int(PREROLL_SEC * SAMPLE_RATE * SAMPLE_WIDTH * CHANNELS)
    if PREFIX_FRAMES > 0:
        preroll_limit = PREFIX_FRAMES * FRAME_BYTES
    if MAX_FRAMES > 0:
        max_segment_bytes = MAX_FRAMES * FRAME_BYTES
    preroll = bytearray()
    speaking = False
    silence_frames = 0
    voiced_frames = 0
    segment = bytearray()

    log_line("listening")
    set_state_field(state, state="listening", last_error="")

    try:
        while RUNNING:
            if recorder.poll() is not None:
                stderr_output = trim((recorder.stderr.read() or b"").decode("utf-8", errors="replace"))
                raise RuntimeError(stderr_output or f"parec exited with code {recorder.returncode}")

            chunk = recorder.stdout.read(FRAME_BYTES)
            if not chunk:
                time.sleep(0.01)
                continue

            rms = int(audioop.rms(chunk, SAMPLE_WIDTH))
            state["last_rms"] = rms
            write_state(state)

            if len(preroll) + len(chunk) > preroll_limit:
                drop = len(preroll) + len(chunk) - preroll_limit
                del preroll[:drop]
            preroll.extend(chunk)

            start_voiced = rms >= START_THRESHOLD
            continue_voiced = rms >= CONTINUE_THRESHOLD
            if start_voiced and not speaking:
                speaking = True
                silence_frames = 0
                voiced_frames = 1
                segment = bytearray(preroll)
                set_state_field(state, state="hearing", last_error="")
                log_line(f"speech started rms={rms}")
            elif speaking:
                segment.extend(chunk)
                if continue_voiced:
                    silence_frames = 0
                    voiced_frames += 1
                else:
                    silence_frames += 1

                should_flush = False
                if len(segment) >= max_segment_bytes:
                    should_flush = True
                    log_line("segment reached max duration")
                elif silence_frames >= max(silence_hold_frames, SILENCE_FRAMES) and len(segment) >= min_speech_bytes:
                    should_flush = True

                if should_flush:
                    if voiced_frames < max(1, MIN_VOICED_FRAMES):
                        log_line(
                            f"dropping short segment voiced_frames={voiced_frames} silence_frames={silence_frames} rms={rms}"
                        )
                        speaking = False
                        silence_frames = 0
                        voiced_frames = 0
                        segment = bytearray()
                        continue
                    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp_file:
                        temp_path = tmp_file.name
                    try:
                        write_wav(temp_path, [bytes(segment)])
                        log_line(f"transcribing bytes={len(segment)} rms={rms}")
                        transcript = transcribe_file(temp_path)
                        log_line(f"heard: {transcript}")
                        set_state_field(
                            state,
                            state="listening",
                            last_heard=transcript,
                            last_partial=transcript,
                            last_error="",
                        )
                    except Exception as exc:
                        log_line(f"error: {trim(exc)}")
                        set_state_field(state, state="listening", last_error=trim(exc))
                    finally:
                        try:
                            os.unlink(temp_path)
                        except OSError:
                            pass
                    speaking = False
                    silence_frames = 0
                    voiced_frames = 0
                    segment = bytearray()
    except Exception as exc:
        log_line(f"fatal: {trim(exc)}")
        set_state_field(state, ok=False, running=False, state="error", last_error=trim(exc))
        return 1
    finally:
        if recorder is not None:
            try:
                recorder.terminate()
            except Exception:
                pass
            try:
                recorder.wait(timeout=2)
            except Exception:
                try:
                    recorder.kill()
                except Exception:
                    pass

    if state.get("state") != "error":
        log_line("stopped")
        set_state_field(state, running=False, state="stopped", last_error="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
