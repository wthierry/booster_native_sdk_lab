#!/usr/bin/env python3

import argparse
import json
import os
import re
import shlex
import subprocess
import time
from datetime import datetime
from pathlib import Path
from typing import Any
from urllib import request


DEFAULT_LLAMA_SERVER = "/home/booster/Workspace/llama.cpp-buildtest/build/bin/llama-server"
DEFAULT_MODEL = "/home/booster/Workspace/models/qwen2.5-1.5b/qwen2.5-1.5b-instruct-q4_k_m.gguf"
DEFAULT_SERVER_HOST = "127.0.0.1"
DEFAULT_SERVER_PORT = 8091
DEFAULT_SERVER_LOG = "/tmp/booster_llama_server.log"
DEFAULT_SERVER_PID = "/tmp/booster_llama_server.pid"
DEFAULT_PIPER = "/usr/local/bin/piper"
DEFAULT_PIPER_MODEL = "/home/booster/voices/piper/en_US-lessac-medium/en_US-lessac-medium.onnx"
DEFAULT_PIPER_CONFIG = "/home/booster/voices/piper/en_US-lessac-medium/en_US-lessac-medium.onnx.json"
DEFAULT_PROMPT = (
    "You are Booster, a helpful robot. "
    "Reply briefly, naturally, and as plain spoken text with no markdown."
)
TIME_QUERY_RE = re.compile(
    r"\b("
    r"time|clock|what\s+time|current\s+time|"
    r"date|today|day\s+is\s+it|what\s+day|"
    r"month|year"
    r")\b",
    re.IGNORECASE,
)


def env_or_default(name: str, default: str) -> str:
    value = os.getenv(name, "").strip()
    return value or default


def first_existing(paths: list[str]) -> str:
    for candidate in paths:
        if Path(candidate).exists():
            return candidate
    return paths[0]


def load_sample_rate(config_path: Path) -> int:
    try:
        data = json.loads(config_path.read_text())
        audio = data.get("audio", {})
        rate = int(audio.get("sample_rate", 22050))
        return rate if rate > 0 else 22050
    except Exception:
        return 22050


def get_default_source() -> str:
    try:
        completed = subprocess.run(
            ["pactl", "get-default-source"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        source = completed.stdout.strip()
        return source
    except Exception:
        return ""


def set_source_mute(source: str, mute: bool) -> None:
    if not source:
        return
    subprocess.run(
        ["pactl", "set-source-mute", source, "1" if mute else "0"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )


def build_prompt(system_prompt: str, user_text: str) -> str:
    return f"System: {system_prompt}\nUser: {user_text}\nAssistant:"


def maybe_answer_from_robot_clock(user_text: str) -> str | None:
    text = user_text.strip()
    if not text or not TIME_QUERY_RE.search(text):
        return None

    now = datetime.now().astimezone()
    lower = text.lower()
    if "date" in lower or "today" in lower or "day" in lower or "month" in lower or "year" in lower:
        return f"Today is {now.strftime('%A, %B %-d, %Y')}."

    hour = now.hour % 12 or 12
    minute = now.minute
    suffix = "AM" if now.hour < 12 else "PM"
    return f"It's {hour}:{minute:02d} {suffix}."


def is_pid_running(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def read_pid(pid_path: Path) -> int:
    try:
        return int(pid_path.read_text().strip())
    except Exception:
        return 0


def write_pid(pid_path: Path, pid: int) -> None:
    pid_path.parent.mkdir(parents=True, exist_ok=True)
    pid_path.write_text(f"{pid}\n")


def http_json(method: str, url: str, payload: dict[str, Any] | None = None, timeout: float = 10.0) -> dict[str, Any]:
    body = None
    headers = {}
    if payload is not None:
        body = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = request.Request(url, data=body, headers=headers, method=method)
    with request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def server_health(server_url: str) -> dict[str, Any] | None:
    try:
        return http_json("GET", server_url.rstrip("/") + "/health", None, timeout=1.5)
    except Exception:
        return None


def ensure_server(server_bin: str, model: str, host: str, port: int, log_path: Path, pid_path: Path) -> tuple[int, list[str], bool]:
    server_url = f"http://{host}:{port}"
    if server_health(server_url) is not None:
        pid = read_pid(pid_path)
        return pid, [], False

    existing_pid = read_pid(pid_path)
    if existing_pid and not is_pid_running(existing_pid):
        try:
            pid_path.unlink()
        except FileNotFoundError:
            pass

    if read_pid(pid_path) <= 0:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        with log_path.open("ab", buffering=0) as log_file:
            cmd = [
                server_bin,
                "-m",
                model,
                "--host",
                host,
                "--port",
                str(port),
                "-n",
                "96",
                "--no-warmup",
            ]
            proc = subprocess.Popen(
                cmd,
                stdin=subprocess.DEVNULL,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            write_pid(pid_path, proc.pid)
        started = True
    else:
        cmd = []
        started = False

    deadline = time.time() + 120.0
    while time.time() < deadline:
        if server_health(server_url) is not None:
            return read_pid(pid_path), cmd, started
        time.sleep(2.0)

    raise RuntimeError(f"llama-server did not become healthy at {server_url}")


def run_server_completion(server_url: str, prompt: str, max_tokens: int) -> tuple[str, dict[str, Any]]:
    payload = {
        "prompt": prompt,
        "n_predict": max_tokens,
        "temperature": 0.2,
        "cache_prompt": True,
        "stop": ["User:", "System:", "<|im_end|>"],
    }
    result = http_json("POST", server_url.rstrip("/") + "/completion", payload, timeout=300.0)
    text = str(result.get("content", "")).strip()
    return " ".join(part.strip() for part in text.splitlines() if part.strip()), result


def speak_text(piper_bin: str, model: str, config: str, text: str) -> tuple[list[str], int, int]:
    sample_rate = load_sample_rate(Path(config))
    piper_cmd = [piper_bin, "-m", model, "-c", config, "--output-raw"]
    aplay_cmd = ["aplay", "-q", "-r", str(sample_rate), "-f", "S16_LE", "-t", "raw", "-c", "1"]
    source = get_default_source()

    set_source_mute(source, True)
    try:
        piper = subprocess.Popen(
            piper_cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=False,
        )
        aplay = subprocess.Popen(
            aplay_cmd,
            stdin=piper.stdout,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=False,
        )
        assert piper.stdin is not None
        piper.stdin.write((text + "\n").encode("utf-8"))
        piper.stdin.close()
        assert piper.stdout is not None
        piper.stdout.close()
        piper_err = b""
        if piper.stderr is not None:
            piper_err = piper.stderr.read()
        piper.wait()
        _, aplay_err = aplay.communicate()
        if piper.returncode != 0:
            raise RuntimeError(f"piper failed: {piper_err.decode('utf-8', errors='ignore').strip()}")
        if aplay.returncode != 0:
            raise RuntimeError(f"aplay failed: {aplay_err.decode('utf-8', errors='ignore').strip()}")
    finally:
        set_source_mute(source, False)
    return piper_cmd + ["|"] + aplay_cmd, piper.returncode, aplay.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="Run a local llama-server prompt and speak the reply with Piper.")
    parser.add_argument("--text", required=True, help="Input text to send to the local LLM.")
    parser.add_argument("--system-prompt", default=env_or_default("BOOSTER_LOCAL_LLM_SYSTEM_PROMPT", DEFAULT_PROMPT))
    parser.add_argument("--server-bin", default=env_or_default("BOOSTER_LOCAL_LLM_SERVER_BIN", DEFAULT_LLAMA_SERVER))
    parser.add_argument("--model", default=env_or_default("BOOSTER_LOCAL_LLM_MODEL", DEFAULT_MODEL))
    parser.add_argument("--host", default=env_or_default("BOOSTER_LOCAL_LLM_SERVER_HOST", DEFAULT_SERVER_HOST))
    parser.add_argument("--port", type=int, default=int(env_or_default("BOOSTER_LOCAL_LLM_SERVER_PORT", str(DEFAULT_SERVER_PORT))))
    parser.add_argument("--max-tokens", type=int, default=int(env_or_default("BOOSTER_LOCAL_LLM_MAX_TOKENS", "96")))
    parser.add_argument("--server-log", default=env_or_default("BOOSTER_LOCAL_LLM_SERVER_LOG", DEFAULT_SERVER_LOG))
    parser.add_argument("--server-pid", default=env_or_default("BOOSTER_LOCAL_LLM_SERVER_PID", DEFAULT_SERVER_PID))
    parser.add_argument("--piper-bin", default=env_or_default("BOOSTER_LOCAL_TTS_PIPER_BIN", DEFAULT_PIPER))
    parser.add_argument(
        "--piper-model",
        default=env_or_default(
            "BOOSTER_LOCAL_TTS_PIPER_MODEL",
            first_existing([DEFAULT_PIPER_MODEL]),
        ),
    )
    parser.add_argument(
        "--piper-config",
        default=env_or_default(
            "BOOSTER_LOCAL_TTS_PIPER_CONFIG",
            first_existing([DEFAULT_PIPER_CONFIG]),
        ),
    )
    args = parser.parse_args()

    server_url = f"http://{args.host}:{args.port}"
    user_text = args.text.strip()
    prompt = build_prompt(args.system_prompt.strip(), user_text)

    try:
        direct_reply = maybe_answer_from_robot_clock(user_text)
        if direct_reply is not None:
            server_pid = read_pid(Path(args.server_pid))
            server_cmd = []
            server_started = False
            reply_text = direct_reply
            raw_result = {
                "source": "robot_clock",
                "timezone": datetime.now().astimezone().tzname(),
                "iso_time": datetime.now().astimezone().isoformat(),
            }
        else:
            server_pid, server_cmd, server_started = ensure_server(
                args.server_bin,
                args.model,
                args.host,
                args.port,
                Path(args.server_log),
                Path(args.server_pid),
            )
            reply_text, raw_result = run_server_completion(server_url, prompt, args.max_tokens)
        if not reply_text:
            print(
                json.dumps(
                    {
                        "ok": False,
                        "error": "local llm produced empty reply",
                        "server_url": server_url,
                        "server_pid": server_pid,
                        "server_started": server_started,
                        "raw_result": raw_result,
                    }
                )
            )
            return 1

        speak_cmd, piper_status, aplay_status = speak_text(
            args.piper_bin,
            args.piper_model,
            args.piper_config,
            reply_text,
        )
        print(
            json.dumps(
                {
                    "ok": True,
                    "input_text": args.text,
                    "reply_text": reply_text,
                    "server_url": server_url,
                    "server_pid": server_pid,
                    "server_started": server_started,
                    "server_cmd": [shlex.quote(part) for part in server_cmd] if server_cmd else [],
                    "speak_cmd": [shlex.quote(part) for part in speak_cmd],
                    "piper_status": piper_status,
                    "aplay_status": aplay_status,
                    "model": args.model,
                    "piper_model": args.piper_model,
                    "raw_result": raw_result,
                }
            )
        )
        return 0
    except Exception as exc:
        print(
            json.dumps(
                {
                    "ok": False,
                    "error": str(exc),
                    "server_url": server_url,
                }
            )
        )
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
