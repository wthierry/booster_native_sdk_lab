#!/usr/bin/env python3

import audioop
import json
import os
import signal
import ssl
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import uuid
import wave
from collections import deque


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OPENAI_KEY_NAMES = ("CHATGPT_API_KEY", "OPENAI_API_KEY", "CHAT_GPT_API", "API_KEY")
RESPONSES_URL = "https://api.openai.com/v1/responses"
TRANSCRIPTIONS_URL = "https://api.openai.com/v1/audio/transcriptions"
SPEECH_URL = "https://api.openai.com/v1/audio/speech"
PID_PATH = "/tmp/booster_openai_robot_voice.pid"
STATUS_PATH = "/tmp/booster_openai_robot_voice_status.json"
LOG_PATH = "/tmp/booster_openai_robot_voice.log"
DEFAULT_TRANSCRIBE_MODEL = "gpt-4o-mini-transcribe"
DEFAULT_TEXT_MODEL = "gpt-4.1-mini"
DEFAULT_TTS_MODEL = "gpt-4o-mini-tts"
DEFAULT_TTS_VOICE = "verse"
DEFAULT_TEXT_SYSTEM_PROMPT = (
    "You are Booster. Reply clearly and briefly. Prefer one short sentence unless more detail is needed."
)
DEFAULT_TTS_INSTRUCTIONS = "Speak naturally, briefly, and clearly."
RUNNING = True
CURRENT_PROCESS = None


def print_json(payload):
    print(json.dumps(payload, ensure_ascii=True))


def now_iso():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def load_dotenv_if_present():
    path = os.path.join(REPO_ROOT, ".env")
    if not os.path.exists(path):
        return

    with open(path, "r", encoding="utf-8") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue

            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip()
            if key.startswith("export "):
                key = key[7:].strip()
            if not key:
                continue
            if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
                value = value[1:-1]
            os.environ.setdefault(key, value)


def build_ssl_context():
    cafile = os.environ.get("SSL_CERT_FILE", "").strip()
    if cafile and os.path.exists(cafile):
        return ssl.create_default_context(cafile=cafile)

    try:
        import certifi

        return ssl.create_default_context(cafile=certifi.where())
    except Exception:
        pass

    if os.path.exists("/etc/ssl/cert.pem"):
        return ssl.create_default_context(cafile="/etc/ssl/cert.pem")

    return ssl.create_default_context()


def resolve_api_key():
    for name in OPENAI_KEY_NAMES:
        value = os.environ.get(name, "").strip()
        if value:
            return value, name
    return "", ""


def write_status(payload):
    status = dict(payload)
    status["updated_at"] = now_iso()
    tmp_path = f"{STATUS_PATH}.{os.getpid()}.tmp"
    with open(tmp_path, "w", encoding="utf-8") as handle:
        json.dump(status, handle, ensure_ascii=True, indent=2)
        handle.write("\n")
    os.replace(tmp_path, STATUS_PATH)


def read_status():
    if not os.path.exists(STATUS_PATH):
        return {}
    try:
        with open(STATUS_PATH, "r", encoding="utf-8") as handle:
            payload = json.load(handle)
            return payload if isinstance(payload, dict) else {}
    except Exception:
        return {}


def read_pid():
    if not os.path.exists(PID_PATH):
        return 0
    try:
        with open(PID_PATH, "r", encoding="utf-8") as handle:
            return int(handle.read().strip() or "0")
    except Exception:
        return 0


def process_alive(pid):
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def remove_runtime_files():
    try:
        os.remove(PID_PATH)
    except FileNotFoundError:
        pass


def signal_handler(signum, frame):
    del signum, frame
    global RUNNING, CURRENT_PROCESS
    RUNNING = False
    if CURRENT_PROCESS and CURRENT_PROCESS.poll() is None:
        try:
            CURRENT_PROCESS.terminate()
        except Exception:
            pass


def run_command(command, capture_output=True, timeout=None):
    return subprocess.run(
        command,
        capture_output=capture_output,
        text=True,
        timeout=timeout,
        check=False,
    )


def resolve_default_source():
    configured = os.environ.get("BOOSTER_OPENAI_ROBOT_SOURCE", "").strip()
    if configured:
        return configured

    result = run_command(["pactl", "get-default-source"], timeout=5)
    if result.returncode == 0 and result.stdout.strip():
        return result.stdout.strip()

    result = run_command(["pactl", "list", "short", "sources"], timeout=5)
    if result.returncode == 0:
        for line in result.stdout.splitlines():
            parts = line.split("\t")
            if len(parts) >= 2 and ".monitor" not in parts[1]:
                return parts[1]
    return ""


def resolve_default_sink():
    configured = os.environ.get("BOOSTER_OPENAI_ROBOT_SINK", "").strip()
    if configured:
        return configured

    result = run_command(["pactl", "get-default-sink"], timeout=5)
    if result.returncode == 0 and result.stdout.strip():
        return result.stdout.strip()

    result = run_command(["pactl", "list", "short", "sinks"], timeout=5)
    if result.returncode == 0:
        for line in result.stdout.splitlines():
            parts = line.split("\t")
            if len(parts) >= 2:
                return parts[1]
    return ""


def encode_multipart_form(parts):
    boundary = f"----booster-{uuid.uuid4().hex}"
    body = bytearray()
    for part in parts:
        name = part["name"]
        content = part["content"]
        content_type = part.get("content_type")
        filename = part.get("filename")
        body.extend(f"--{boundary}\r\n".encode("utf-8"))
        disposition = f'Content-Disposition: form-data; name="{name}"'
        if filename:
            disposition += f'; filename="{filename}"'
        body.extend((disposition + "\r\n").encode("utf-8"))
        if content_type:
            body.extend(f"Content-Type: {content_type}\r\n".encode("utf-8"))
        body.extend(b"\r\n")
        if isinstance(content, str):
            body.extend(content.encode("utf-8"))
        else:
            body.extend(content)
        body.extend(b"\r\n")
    body.extend(f"--{boundary}--\r\n".encode("utf-8"))
    return boundary, bytes(body)


def extract_output_text(payload):
    text = payload.get("output_text")
    if isinstance(text, str) and text.strip():
        return text.strip()

    pieces = []
    for item in payload.get("output", []):
        for content in item.get("content", []):
            if content.get("type") == "output_text":
                chunk = content.get("text", "")
                if isinstance(chunk, str) and chunk.strip():
                    pieces.append(chunk.strip())
    return "\n".join(pieces).strip()


def openai_json_request(url, request_body, timeout=30):
    api_key, key_name = resolve_api_key()
    if not api_key:
        return {
            "ok": False,
            "code": 503,
            "error": "Missing OpenAI API key",
        }, ""

    request = urllib.request.Request(
        url,
        data=json.dumps(request_body).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=timeout, context=build_ssl_context()) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        return {
            "ok": False,
            "code": exc.code,
            "error": "OpenAI request failed",
            "response_body": exc.read().decode("utf-8", errors="replace"),
        }, key_name
    except Exception as exc:
        return {
            "ok": False,
            "code": 500,
            "error": f"OpenAI request error: {exc}",
        }, key_name

    return {
        "ok": True,
        "code": 0,
        "response_body": payload,
    }, key_name


def transcribe_audio(audio_path, config):
    api_key, key_name = resolve_api_key()
    if not api_key:
        return {
            "ok": False,
            "code": 503,
            "error": "Missing OpenAI API key",
        }

    with open(audio_path, "rb") as handle:
        audio_bytes = handle.read()

    boundary, body = encode_multipart_form([
        {"name": "model", "content": config["transcribe_model"]},
        {"name": "response_format", "content": "json"},
        {"name": "language", "content": config["language"]},
        {
            "name": "file",
            "filename": os.path.basename(audio_path),
            "content": audio_bytes,
            "content_type": "audio/wav",
        },
    ])

    request = urllib.request.Request(
        TRANSCRIPTIONS_URL,
        data=body,
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": f"multipart/form-data; boundary={boundary}",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=45, context=build_ssl_context()) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        return {
            "ok": False,
            "code": exc.code,
            "error": "OpenAI transcription failed",
            "response_body": exc.read().decode("utf-8", errors="replace"),
        }
    except Exception as exc:
        return {
            "ok": False,
            "code": 500,
            "error": f"OpenAI transcription error: {exc}",
        }

    return {
        "ok": True,
        "code": 0,
        "api_key_name": key_name,
        "model": config["transcribe_model"],
        "text": str(payload.get("text", "")).strip(),
        "response_body": payload,
    }


def respond_text(text, config):
    request_body = {
        "model": config["text_model"],
        "input": [
            {
                "role": "system",
                "content": [{"type": "input_text", "text": config["system_prompt"]}],
            },
            {
                "role": "user",
                "content": [{"type": "input_text", "text": text}],
            },
        ],
        "max_output_tokens": 220,
    }
    result, key_name = openai_json_request(RESPONSES_URL, request_body, timeout=30)
    if not result.get("ok"):
        return result
    return {
        "ok": True,
        "code": 0,
        "api_key_name": key_name,
        "model": config["text_model"],
        "answer": extract_output_text(result["response_body"]),
        "response_body": result["response_body"],
    }


def synthesize_speech(text, output_path, config):
    api_key, key_name = resolve_api_key()
    if not api_key:
        return {
            "ok": False,
            "code": 503,
            "error": "Missing OpenAI API key",
        }

    request_body = {
        "model": config["tts_model"],
        "voice": config["tts_voice"],
        "input": text,
        "instructions": config["tts_instructions"],
        "response_format": "wav",
    }

    request = urllib.request.Request(
        SPEECH_URL,
        data=json.dumps(request_body).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=60, context=build_ssl_context()) as response:
            audio_bytes = response.read()
    except urllib.error.HTTPError as exc:
        return {
            "ok": False,
            "code": exc.code,
            "error": "OpenAI speech synthesis failed",
            "response_body": exc.read().decode("utf-8", errors="replace"),
        }
    except Exception as exc:
        return {
            "ok": False,
            "code": 500,
            "error": f"OpenAI speech synthesis error: {exc}",
        }

    with open(output_path, "wb") as handle:
        handle.write(audio_bytes)

    return {
        "ok": True,
        "code": 0,
        "api_key_name": key_name,
        "model": config["tts_model"],
        "voice": config["tts_voice"],
        "audio_path": output_path,
    }


def play_audio(audio_path, sink_name):
    command = ["paplay"]
    if sink_name:
        command.append(f"--device={sink_name}")
    command.append(audio_path)
    proc = subprocess.run(command, capture_output=True, text=True, timeout=120, check=False)
    if proc.returncode != 0:
        return {
            "ok": False,
            "code": proc.returncode,
            "error": "Audio playback failed",
            "stderr": proc.stderr.strip(),
        }
    return {
        "ok": True,
        "code": 0,
    }


def write_wav(audio_bytes, output_path):
    with wave.open(output_path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(16000)
        handle.writeframes(audio_bytes)


def load_config():
    return {
        "transcribe_model": os.environ.get("BOOSTER_OPENAI_TRANSCRIBE_MODEL", "").strip()
        or DEFAULT_TRANSCRIBE_MODEL,
        "text_model": os.environ.get("BOOSTER_OPENAI_TEXT_MODEL", "").strip() or DEFAULT_TEXT_MODEL,
        "tts_model": os.environ.get("BOOSTER_OPENAI_ROBOT_TTS_MODEL", "").strip() or DEFAULT_TTS_MODEL,
        "tts_voice": os.environ.get("BOOSTER_OPENAI_ROBOT_TTS_VOICE", "").strip()
        or os.environ.get("BOOSTER_OPENAI_REALTIME_VOICE", "").strip()
        or DEFAULT_TTS_VOICE,
        "system_prompt": os.environ.get("BOOSTER_OPENAI_TEXT_SYSTEM_PROMPT", "").strip()
        or DEFAULT_TEXT_SYSTEM_PROMPT,
        "tts_instructions": os.environ.get("BOOSTER_OPENAI_ROBOT_TTS_INSTRUCTIONS", "").strip()
        or DEFAULT_TTS_INSTRUCTIONS,
        "language": os.environ.get("BOOSTER_OPENAI_TRANSCRIBE_LANGUAGE", "").strip() or "en",
        "vad_threshold": max(100, int(os.environ.get("BOOSTER_OPENAI_ROBOT_VAD_THRESHOLD", "900"))),
        "vad_silence_frames": max(2, int(os.environ.get("BOOSTER_OPENAI_ROBOT_VAD_SILENCE_FRAMES", "8"))),
        "vad_prefix_frames": max(1, int(os.environ.get("BOOSTER_OPENAI_ROBOT_VAD_PREFIX_FRAMES", "3"))),
        "vad_min_frames": max(1, int(os.environ.get("BOOSTER_OPENAI_ROBOT_VAD_MIN_FRAMES", "3"))),
        "max_utterance_frames": max(20, int(os.environ.get("BOOSTER_OPENAI_ROBOT_MAX_UTTERANCE_FRAMES", "150"))),
        "source": resolve_default_source(),
        "sink": resolve_default_sink(),
    }


def status_payload(extra=None):
    config = load_config()
    pid = read_pid()
    alive = process_alive(pid)
    payload = read_status()
    payload.pop("action", None)
    payload.pop("spawned_pid", None)
    payload.update({
        "available": bool(resolve_api_key()[0]),
        "pid": pid if alive else 0,
        "running": alive,
        "source": config["source"],
        "sink": config["sink"],
        "transcribe_model": config["transcribe_model"],
        "text_model": config["text_model"],
        "tts_model": config["tts_model"],
        "tts_voice": config["tts_voice"],
        "log_path": LOG_PATH,
    })
    if not alive and payload.get("state") not in {"error", "stopped"}:
        payload["state"] = "stopped"
    if extra:
        payload.update(extra)
    return payload


def start_worker():
    existing_pid = read_pid()
    if process_alive(existing_pid):
        payload = status_payload({"ok": True, "action": "start", "note": "Robot OpenAI voice is already running."})
        write_status(payload)
        return payload

    if not resolve_api_key()[0]:
        payload = status_payload({
            "ok": False,
            "action": "start",
            "error": "Missing OpenAI API key",
            "state": "error",
        })
        write_status(payload)
        return payload

    with open(LOG_PATH, "ab") as log_handle:
        proc = subprocess.Popen(
            [sys.executable, os.path.abspath(__file__), "run"],
            cwd=REPO_ROOT,
            stdin=subprocess.DEVNULL,
            stdout=log_handle,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )

    time.sleep(0.4)
    payload = status_payload({"ok": True, "action": "start", "spawned_pid": proc.pid})
    if not payload.get("running"):
        payload["state"] = "starting"
    return payload


def stop_worker():
    pid = read_pid()
    if not process_alive(pid):
        payload = status_payload({"ok": True, "action": "stop", "state": "stopped"})
        write_status(payload)
        remove_runtime_files()
        return payload

    try:
        os.kill(pid, signal.SIGTERM)
    except OSError as exc:
        payload = status_payload({"ok": False, "action": "stop", "error": str(exc), "state": "error"})
        write_status(payload)
        return payload

    deadline = time.time() + 5
    while time.time() < deadline:
        if not process_alive(pid):
            break
        time.sleep(0.1)

    payload = status_payload({"ok": True, "action": "stop", "state": "stopped"})
    write_status(payload)
    if not process_alive(pid):
        remove_runtime_files()
    return payload


def run_worker():
    global CURRENT_PROCESS, RUNNING
    signal.signal(signal.SIGTERM, signal_handler)
    signal.signal(signal.SIGINT, signal_handler)
    config = load_config()
    if not config["source"]:
        write_status(status_payload({
            "ok": False,
            "state": "error",
            "error": "No PulseAudio input source found for robot microphone.",
        }))
        return 1
    if not config["sink"]:
        write_status(status_payload({
            "ok": False,
            "state": "error",
            "error": "No PulseAudio sink found for robot speaker output.",
        }))
        return 1

    with open(PID_PATH, "w", encoding="utf-8") as handle:
        handle.write(str(os.getpid()))

    write_status(status_payload({
        "ok": True,
        "state": "listening",
        "started_at": now_iso(),
        "last_heard": "",
        "last_spoken": "",
        "last_error": "",
    }))

    frame_bytes = 3200
    prefix_frames = deque(maxlen=config["vad_prefix_frames"])
    utterance = bytearray()
    voiced_frames = 0
    silent_frames = 0
    speaking = False

    CURRENT_PROCESS = subprocess.Popen(
        [
            "parec",
            f"--device={config['source']}",
            "--rate=16000",
            "--channels=1",
            "--format=s16le",
            "--raw",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        bufsize=0,
    )

    try:
        while RUNNING and CURRENT_PROCESS.poll() is None:
            chunk = CURRENT_PROCESS.stdout.read(frame_bytes)
            if not chunk:
                break

            rms = audioop.rms(chunk, 2)
            if not speaking:
                prefix_frames.append(chunk)
                if rms >= config["vad_threshold"]:
                    speaking = True
                    utterance = bytearray().join(prefix_frames)
                    prefix_frames.clear()
                    voiced_frames = 1
                    silent_frames = 0
                    write_status(status_payload({"ok": True, "state": "hearing"}))
                continue

            utterance.extend(chunk)
            if rms >= config["vad_threshold"]:
                voiced_frames += 1
                silent_frames = 0
            else:
                silent_frames += 1

            should_flush = silent_frames >= config["vad_silence_frames"] or voiced_frames >= config["max_utterance_frames"]
            if not should_flush:
                continue

            if voiced_frames >= config["vad_min_frames"]:
                with tempfile.TemporaryDirectory(prefix="booster_openai_voice_") as tmpdir:
                    input_path = os.path.join(tmpdir, "input.wav")
                    output_path = os.path.join(tmpdir, "reply.wav")
                    write_wav(bytes(utterance), input_path)

                    write_status(status_payload({"ok": True, "state": "transcribing"}))
                    transcription = transcribe_audio(input_path, config)
                    if not transcription.get("ok"):
                        write_status(status_payload({
                            "ok": False,
                            "state": "error",
                            "last_error": transcription.get("error", "Transcription failed"),
                        }))
                    else:
                        heard = transcription.get("text", "").strip()
                        if heard:
                            write_status(status_payload({
                                "ok": True,
                                "state": "thinking",
                                "last_heard": heard,
                                "last_error": "",
                            }))
                            response = respond_text(heard, config)
                            if not response.get("ok"):
                                write_status(status_payload({
                                    "ok": False,
                                    "state": "error",
                                    "last_heard": heard,
                                    "last_error": response.get("error", "Text response failed"),
                                }))
                            else:
                                answer = response.get("answer", "").strip()
                                if answer:
                                    write_status(status_payload({
                                        "ok": True,
                                        "state": "synthesizing",
                                        "last_heard": heard,
                                        "last_spoken": answer,
                                        "last_error": "",
                                    }))
                                    speech = synthesize_speech(answer, output_path, config)
                                    if not speech.get("ok"):
                                        write_status(status_payload({
                                            "ok": False,
                                            "state": "error",
                                            "last_heard": heard,
                                            "last_spoken": answer,
                                            "last_error": speech.get("error", "Speech synthesis failed"),
                                        }))
                                    else:
                                        write_status(status_payload({
                                            "ok": True,
                                            "state": "speaking",
                                            "last_heard": heard,
                                            "last_spoken": answer,
                                            "last_error": "",
                                        }))
                                        playback = play_audio(output_path, config["sink"])
                                        if not playback.get("ok"):
                                            write_status(status_payload({
                                                "ok": False,
                                                "state": "error",
                                                "last_heard": heard,
                                                "last_spoken": answer,
                                                "last_error": playback.get("error", "Playback failed"),
                                            }))
                                        else:
                                            write_status(status_payload({
                                                "ok": True,
                                                "state": "listening",
                                                "last_heard": heard,
                                                "last_spoken": answer,
                                                "last_error": "",
                                            }))
            speaking = False
            utterance = bytearray()
            voiced_frames = 0
            silent_frames = 0
            prefix_frames.clear()

        if RUNNING:
            write_status(status_payload({
                "ok": False,
                "state": "error",
                "last_error": "Robot microphone stream stopped unexpectedly.",
            }))
    finally:
        if CURRENT_PROCESS and CURRENT_PROCESS.poll() is None:
            try:
                CURRENT_PROCESS.terminate()
            except Exception:
                pass
        CURRENT_PROCESS = None
        remove_runtime_files()
        if RUNNING:
            write_status(status_payload({"ok": False, "state": "stopped"}))
        else:
            write_status(status_payload({"ok": True, "state": "stopped"}))
    return 0


def main():
    load_dotenv_if_present()
    action = sys.argv[1] if len(sys.argv) > 1 else "status"

    if action == "start":
        result = start_worker()
        print_json(result)
        return 0 if result.get("ok") else 1

    if action == "stop":
        result = stop_worker()
        print_json(result)
        return 0 if result.get("ok") else 1

    if action == "status":
        result = status_payload({"ok": True, "action": "status"})
        print_json(result)
        return 0

    if action == "run":
        return run_worker()

    print_json({
        "ok": False,
        "code": 400,
        "error": "Usage: openai_robot_voice.py <start|stop|status|run>",
    })
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
