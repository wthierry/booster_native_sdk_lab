#!/usr/bin/env python3

import asyncio
import audioop
import base64
import datetime as dt
import importlib.util
import json
import os
import signal
import subprocess
import tempfile
import time
import uuid
import wave


if importlib.util.find_spec("websockets") is None:
    raise SystemExit("missing dependency: websockets")

import websockets


SAMPLE_RATE = int(os.getenv("BOOSTER_OPENAI_REALTIME_SAMPLE_RATE", "24000"))
SAMPLE_WIDTH = 2
CHANNELS = int(os.getenv("BOOSTER_OPENAI_REALTIME_CHANNELS", "1"))
FRAME_SAMPLES = int(os.getenv("BOOSTER_OPENAI_REALTIME_FRAME_SAMPLES", "1024"))
FRAME_BYTES = FRAME_SAMPLES * SAMPLE_WIDTH * CHANNELS
STATE_PATH = os.getenv("BOOSTER_OPENAI_REALTIME_STATE_PATH", "/tmp/booster_openai_realtime_state.json")
LOG_PATH = os.getenv("BOOSTER_OPENAI_REALTIME_LOG_PATH", "/tmp/booster_openai_realtime.log")
API_URL = (os.getenv("BOOSTER_OPENAI_REALTIME_URL", "wss://api.openai.com/v1/realtime").strip()
           or "wss://api.openai.com/v1/realtime")
MODEL = (os.getenv("BOOSTER_OPENAI_REALTIME_MODEL", "gpt-realtime-2").strip()
         or "gpt-realtime-2")
TRANSCRIPTION_MODEL = (os.getenv("BOOSTER_OPENAI_REALTIME_TRANSCRIPTION_MODEL", "gpt-4o-mini-transcribe").strip()
                       or "gpt-4o-mini-transcribe")
LANGUAGE = (os.getenv("BOOSTER_OPENAI_REALTIME_LANGUAGE", "en").strip()
            or "en")
INSTRUCTIONS = (os.getenv(
    "BOOSTER_OPENAI_REALTIME_INSTRUCTIONS",
    "You are Booster, a concise robot voice assistant. Reply in one short sentence.",
).strip() or "You are Booster, a concise robot voice assistant. Reply in one short sentence.")
SOURCE = os.getenv("BOOSTER_OPENAI_REALTIME_SOURCE", "").strip()
API_KEY = (os.getenv("OPENAI_API_KEY", "").strip()
           or os.getenv("CHATGPT_API_KEY", "").strip()
           or os.getenv("CHAT_GPT_API", "").strip())
VAD_THRESHOLD = float(os.getenv("BOOSTER_OPENAI_REALTIME_VAD_THRESHOLD", "0.5"))
VAD_PREFIX_MS = int(os.getenv("BOOSTER_OPENAI_REALTIME_VAD_PREFIX_MS", "300"))
VAD_SILENCE_MS = int(os.getenv("BOOSTER_OPENAI_REALTIME_VAD_SILENCE_MS", "650"))
MAX_OUTPUT_TOKENS = os.getenv("BOOSTER_OPENAI_REALTIME_MAX_OUTPUT_TOKENS", "128").strip() or "128"
NOISE_REDUCTION = os.getenv("BOOSTER_OPENAI_REALTIME_NOISE_REDUCTION", "far_field").strip()
VOICE = (os.getenv("BOOSTER_OPENAI_REALTIME_VOICE", "alloy").strip()
         or "alloy")
ENABLE_AUDIO_OUTPUT = os.getenv("BOOSTER_OPENAI_REALTIME_ENABLE_AUDIO_OUTPUT", "1").strip() != "0"
OUTPUT_PLAYER = (os.getenv("BOOSTER_OPENAI_REALTIME_OUTPUT_PLAYER", "paplay").strip()
                 or "paplay")
PLAYBACK_COOLDOWN_MS = int(os.getenv("BOOSTER_OPENAI_REALTIME_PLAYBACK_COOLDOWN_MS", "250"))

RUNNING = True


def iso_now():
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def trim(value):
    return str(value or "").strip()


def log_line(message):
    log_dir = os.path.dirname(LOG_PATH)
    if log_dir:
        os.makedirs(log_dir, exist_ok=True)
    with open(LOG_PATH, "a", encoding="utf-8") as handle:
        handle.write(f"{iso_now()} {message}\n")


def write_state(payload):
    payload = dict(payload)
    payload["updated_at"] = iso_now()
    state_dir = os.path.dirname(STATE_PATH) or "."
    os.makedirs(state_dir, exist_ok=True)
    fd, tmp_path = tempfile.mkstemp(prefix=".booster_openai_realtime_state.", suffix=".tmp", dir=state_dir)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, ensure_ascii=True, indent=2)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(tmp_path, STATE_PATH)
    finally:
        try:
            if os.path.exists(tmp_path):
                os.unlink(tmp_path)
        except OSError:
            pass


def set_state_field(state_data, **updates):
    state_data.update(updates)
    write_state(state_data)


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


def flatten_text(value):
    if isinstance(value, str):
        return value
    if isinstance(value, list):
        parts = []
        for item in value:
            if isinstance(item, dict):
                parts.append(flatten_text(item.get("text") or item.get("transcript") or item.get("content")))
            else:
                parts.append(flatten_text(item))
        return " ".join(part for part in parts if part).strip()
    if isinstance(value, dict):
        return flatten_text(value.get("text") or value.get("transcript") or value.get("content"))
    return ""


def extract_response_text(event):
    response = event.get("response")
    if not isinstance(response, dict):
        return ""
    outputs = response.get("output")
    if not isinstance(outputs, list):
        return ""
    parts = []
    for output in outputs:
        if not isinstance(output, dict):
            continue
        content = output.get("content")
        if isinstance(content, list):
            for item in content:
                if not isinstance(item, dict):
                    continue
                text = flatten_text(item.get("text") or item.get("transcript"))
                if text:
                    parts.append(text)
    return " ".join(part for part in parts if part).strip()


async def maybe_send(ws, payload):
    await ws.send(json.dumps(payload))


def build_output_player_command():
    if OUTPUT_PLAYER == "aplay":
        return ["aplay", "-q"]
    return ["paplay"]


def stop_output_player(player):
    if player is None:
        return None
    try:
        player.terminate()
    except Exception:
        pass
    try:
        player.wait(timeout=1)
    except Exception:
        try:
            player.kill()
        except Exception:
            pass
    return None


def play_wav_file(wav_path, player):
    player = stop_output_player(player)
    try:
        command = build_output_player_command() + [wav_path]
        log_line(f"audio_output_start: {' '.join(command)}")
        return subprocess.Popen(
            command,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
    except Exception as exc:
        log_line(f"audio_output_error: failed to start {OUTPUT_PLAYER}: {trim(exc)}")
        return None


def write_wav_file(audio_bytes):
    fd, wav_path = tempfile.mkstemp(prefix=".booster_openai_realtime_audio.", suffix=".wav")
    os.close(fd)
    with wave.open(wav_path, "wb") as wav_file:
        wav_file.setnchannels(CHANNELS)
        wav_file.setsampwidth(SAMPLE_WIDTH)
        wav_file.setframerate(SAMPLE_RATE)
        wav_file.writeframes(audio_bytes)
    return wav_path


async def stream_mic(ws, recorder, state, runtime):
    while RUNNING:
        if recorder.poll() is not None:
            stderr_output = trim((recorder.stderr.read() or b"").decode("utf-8", errors="replace"))
            raise RuntimeError(stderr_output or f"parec exited with code {recorder.returncode}")
        chunk = await asyncio.to_thread(recorder.stdout.read, FRAME_BYTES)
        if not chunk:
            await asyncio.sleep(0.01)
            continue
        rms = int(audioop.rms(chunk, SAMPLE_WIDTH))
        state["last_rms"] = rms
        write_state(state)
        output_player = runtime.get("output_player")
        if output_player is not None:
            player_exit = output_player.poll()
            if player_exit is None:
                continue
            stderr_output = trim(output_player.stderr.read() or "") if output_player.stderr else ""
            if player_exit != 0:
                log_line(f"audio_output_error: {OUTPUT_PLAYER} exited with code {player_exit}: {stderr_output}")
            else:
                log_line(f"audio_output_done: {OUTPUT_PLAYER} exited with code 0")
            runtime["output_player"] = None
            runtime["mic_unmute_at"] = time.monotonic() + (PLAYBACK_COOLDOWN_MS / 1000.0)
        if time.monotonic() < runtime.get("mic_unmute_at", 0.0):
            continue
        await maybe_send(ws, {
            "type": "input_audio_buffer.append",
            "audio": base64.b64encode(chunk).decode("ascii"),
        })


async def consume_events(ws, state, runtime):
    response_text = {}
    response_audio = {}
    response_requested_for_item = set()
    output_player = None
    current_wav_path = None
    while RUNNING:
        try:
            raw = await asyncio.wait_for(ws.recv(), timeout=0.5)
        except asyncio.TimeoutError:
            continue
        except websockets.ConnectionClosed:
            break
        event = json.loads(raw)
        event_type = event.get("type", "")
        if event_type == "session.created":
            log_line("session_created")
        elif event_type == "session.updated":
            log_line("session_updated")
            set_state_field(state, state="listening", last_error="")
        elif event_type == "input_audio_buffer.speech_started":
            log_line("speech_started")
            output_player = stop_output_player(output_player)
            runtime["output_player"] = None
            runtime["mic_unmute_at"] = time.monotonic()
            set_state_field(state, state="hearing", last_error="")
        elif event_type == "input_audio_buffer.speech_stopped":
            log_line("speech_stopped")
        elif event_type == "conversation.item.input_audio_transcription.delta":
            delta = trim(event.get("delta"))
            if delta:
                set_state_field(state, last_partial=delta)
        elif event_type == "conversation.item.input_audio_transcription.completed":
            transcript = trim(event.get("transcript"))
            item_id = trim(event.get("item_id"))
            if transcript:
                log_line(f"heard: {transcript}")
                set_state_field(state, state="thinking", last_heard=transcript, last_partial=transcript, last_error="")
                if item_id and item_id not in response_requested_for_item:
                    response_requested_for_item.add(item_id)
                    await maybe_send(ws, {
                        "type": "response.create",
                        "response": {
                            "output_modalities": ["audio"],
                            "max_output_tokens": int(MAX_OUTPUT_TOKENS),
                        },
                    })
            else:
                set_state_field(state, state="listening", last_error="")
        elif event_type in ("response.audio.delta", "response.output_audio.delta"):
            if not ENABLE_AUDIO_OUTPUT:
                continue
            response_id = trim(event.get("response_id"))
            audio_b64 = trim(event.get("delta"))
            if not audio_b64 or not response_id:
                continue
            try:
                chunk = base64.b64decode(audio_b64)
                response_audio[response_id] = response_audio.get(response_id, b"") + chunk
            except Exception as exc:
                log_line(f"audio_output_error: decode failed: {trim(exc)}")
        elif event_type in ("response.audio.done", "response.output_audio.done"):
            pass
        elif event_type == "response.output_text.delta":
            response_id = trim(event.get("response_id"))
            delta = event.get("delta") or ""
            response_text[response_id] = response_text.get(response_id, "") + delta
            partial = trim(response_text[response_id])
            if partial:
                set_state_field(state, state="responding", last_spoken_partial=partial, last_error="")
        elif event_type == "response.output_text.done":
            response_id = trim(event.get("response_id"))
            text = trim(event.get("text") or response_text.get(response_id, ""))
            if text:
                log_line(f"response: {text}")
                set_state_field(state, state="listening", last_spoken=text, last_spoken_partial=text, last_error="")
        elif event_type == "response.done":
            response_id = trim(event.get("response", {}).get("id"))
            text = trim(response_text.pop(response_id, "")) or extract_response_text(event)
            audio_bytes = response_audio.pop(response_id, b"")
            if ENABLE_AUDIO_OUTPUT:
                log_line(f"audio_output_bytes: {len(audio_bytes)}")
            if ENABLE_AUDIO_OUTPUT and audio_bytes:
                try:
                    if current_wav_path and os.path.exists(current_wav_path):
                        os.unlink(current_wav_path)
                    current_wav_path = write_wav_file(audio_bytes)
                    output_player = play_wav_file(current_wav_path, output_player)
                    runtime["output_player"] = output_player
                    runtime["mic_unmute_at"] = 0.0
                except Exception as exc:
                    log_line(f"audio_output_error: wav playback failed: {trim(exc)}")
            elif ENABLE_AUDIO_OUTPUT:
                log_line("audio_output_warning: response had no audio payload")
            if text:
                log_line(f"response_done: {text}")
                set_state_field(state, state="listening", last_spoken=text, last_spoken_partial=text, last_error="")
            else:
                set_state_field(state, state="listening", last_error="")
        elif event_type == "error":
            error = event.get("error") or {}
            message = trim(error.get("message") or event.get("message") or json.dumps(event, ensure_ascii=True))
            log_line(f"error: {message}")
            output_player = stop_output_player(output_player)
            set_state_field(state, state="error", last_error=message)
    stop_output_player(output_player)
    try:
        if current_wav_path and os.path.exists(current_wav_path):
            os.unlink(current_wav_path)
    except OSError:
        pass


async def run():
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
        "last_spoken_partial": "",
        "backend": "openai_realtime",
        "label": "OpenAI Realtime",
        "log_path": LOG_PATH,
        "model": MODEL,
        "transcription_model": TRANSCRIPTION_MODEL,
        "language": LANGUAGE,
        "source": source,
        "sample_rate_hz": SAMPLE_RATE,
        "frame_samples": FRAME_SAMPLES,
        "api_url": API_URL,
        "voice": VOICE,
        "audio_output_enabled": ENABLE_AUDIO_OUTPUT,
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
        message = f"failed to start parec: {trim(exc)}"
        log_line(f"fatal: {message}")
        set_state_field(state, ok=False, running=False, state="error", last_error=message)
        return 1

    session_update = {
        "type": "session.update",
        "session": {
            "type": "realtime",
            "model": MODEL,
            "instructions": INSTRUCTIONS,
            "output_modalities": ["audio"],
            "audio": {
                "input": {
                    "format": {
                        "type": "audio/pcm",
                        "rate": SAMPLE_RATE,
                    },
                    "transcription": {
                        "model": TRANSCRIPTION_MODEL,
                        "language": LANGUAGE,
                    },
                    "turn_detection": {
                        "type": "server_vad",
                        "threshold": VAD_THRESHOLD,
                        "prefix_padding_ms": VAD_PREFIX_MS,
                        "silence_duration_ms": VAD_SILENCE_MS,
                        "interrupt_response": True,
                        "create_response": False,
                    },
                },
                "output": {
                    "format": {
                        "type": "audio/pcm",
                        "rate": SAMPLE_RATE,
                    },
                    "voice": VOICE,
                },
            },
        },
    }

    uri = f"{API_URL}?model={MODEL}"
    headers = {
        "Authorization": f"Bearer {API_KEY}",
    }

    try:
        runtime = {
            "output_player": None,
            "mic_unmute_at": 0.0,
        }
        async with websockets.connect(uri, extra_headers=headers, max_size=None) as ws:
            log_line("realtime_connected")
            await maybe_send(ws, session_update)
            await asyncio.gather(stream_mic(ws, recorder, state, runtime), consume_events(ws, state, runtime))
    except Exception as exc:
        message = trim(exc)
        log_line(f"fatal: {message}")
        set_state_field(state, ok=False, running=False, state="error", last_error=message)
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
    raise SystemExit(asyncio.run(run()))
