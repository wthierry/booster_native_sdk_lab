#!/usr/bin/env python3

import json
import os
import re
import subprocess
import sys


WELCOME_MSG = ""
SYSTEM_PROMPT = """
## prompt
You're a robot named Booster that offers emotional chatting to users with a dry, sarcastic sense of humor.
Always respond in English.
Be witty, teasing, and lightly deadpan while still being genuinely helpful.
Keep the sarcasm playful and clever, not cruel or bullying.
Keep responses to exactly 1 short sentence unless the user explicitly asks for more detail.
Do not ask follow-up questions.
Do not add a second sentence unless it is required to answer correctly.
Do not continue the conversation on your own after answering.


## skill
I am a professional chatbot capable of providing conversation services with a variety of voices.
My style is clever, mildly sarcastic, and entertaining.
""".strip()

API_START_AI_CHAT = 2000
API_STOP_AI_CHAT = 2001
SERVICE_TYPE = "booster_interface/srv/RpcService"
SERVICE_NAME = "booster_rtc_service"
DEFAULT_INTERRUPT_SPEECH_DURATION_MS = 700
DEFAULT_INTERRUPT_KEYWORDS = ["stop", "shut up"]


def print_json(payload):
    print(json.dumps(payload, ensure_ascii=True))


def resolve_voice_type(payload):
    requested = str(payload.get("voice_type", "") or "").strip()
    if requested:
        return requested
    configured = os.getenv("BOOSTER_RTC_VOICE_TYPE", "").strip()
    if configured:
        return configured
    return "zh_female_shuangkuaisisi_emo_v2_mars_bigtts"


def parse_response(output):
    status_matches = re.findall(r"status=(\d+)", output)
    body_matches = re.findall(r"body='((?:\\'|[^'])*)'", output, re.DOTALL)
    if not status_matches:
        return None

    status = int(status_matches[-1])
    body = ""
    if body_matches:
        body = body_matches[-1].replace("\\'", "'")

    payload = {
        "ok": status == 0,
        "code": status,
    }
    if body:
        try:
            payload["response_body"] = json.loads(body)
        except Exception:
            payload["response_body"] = body
    return payload


def send_rpc(api_id, body, action):
    request_payload = json.dumps(
        {
            "msg": {
                "api_id": api_id,
                "body": body,
            }
        },
        ensure_ascii=False,
    )
    command = [
        "ros2",
        "service",
        "call",
        f"/{SERVICE_NAME}",
        SERVICE_TYPE,
        request_payload,
    ]

    try:
        proc = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return {
            "ok": False,
            "code": 504,
            "action": action,
            "error": "ROS RTC service request timed out",
        }
    except FileNotFoundError:
        return {
            "ok": False,
            "code": 503,
            "action": action,
            "error": "ros2 CLI not available in PATH",
        }

    combined_output = "\n".join(part for part in [proc.stdout.strip(), proc.stderr.strip()] if part).strip()
    parsed = parse_response(combined_output)
    if parsed is None:
        return {
            "ok": False,
            "code": proc.returncode or 500,
            "action": action,
            "error": "Unexpected ros2 service output",
            "raw_output": combined_output,
        }

    parsed["action"] = action
    parsed["cli_exit_status"] = proc.returncode
    parsed["service_name"] = f"/{SERVICE_NAME}"
    return parsed


def main():
    if len(sys.argv) != 3:
        print_json({
            "ok": False,
            "code": 400,
            "action": "usage",
            "error": "Usage: ros_rtc_tts.py <start|stop> <json-payload>",
        })
        return 1

    action = sys.argv[1]
    try:
        payload = json.loads(sys.argv[2])
    except Exception as exc:
        print_json({
            "ok": False,
            "code": 400,
            "action": action,
            "error": f"Invalid JSON payload: {exc}",
        })
        return 1

    if action == "start":
        voice_type = resolve_voice_type(payload)
        interrupt_speech_duration = int(
            payload.get("interrupt_speech_duration", DEFAULT_INTERRUPT_SPEECH_DURATION_MS)
        )
        if interrupt_speech_duration < 0:
            interrupt_speech_duration = DEFAULT_INTERRUPT_SPEECH_DURATION_MS
        interrupt_keywords = payload.get("interrupt_keywords", DEFAULT_INTERRUPT_KEYWORDS)
        if not isinstance(interrupt_keywords, list):
            interrupt_keywords = list(DEFAULT_INTERRUPT_KEYWORDS)
        interrupt_keywords = [str(keyword).strip() for keyword in interrupt_keywords if str(keyword).strip()]
        if not interrupt_keywords:
            interrupt_keywords = list(DEFAULT_INTERRUPT_KEYWORDS)
        request_payload = {
            "interrupt_mode": True,
            "asr_config": {
                "interrupt_speech_duration": interrupt_speech_duration,
                "interrupt_keywords": interrupt_keywords,
            },
            "llm_config": {
                "system_prompt": SYSTEM_PROMPT,
                "welcome_msg": WELCOME_MSG,
                "prompt_name": "",
            },
            "tts_config": {
                "ignore_bracket_text": [3],
            },
            "enable_face_tracking": False,
        }
        if voice_type:
            request_payload["tts_config"]["voice_type"] = voice_type
        body = json.dumps(request_payload)
        result = send_rpc(API_START_AI_CHAT, body, "start_tts")
        result["voice_type"] = voice_type
        result["interrupt_speech_duration"] = interrupt_speech_duration
        result["interrupt_keywords"] = interrupt_keywords
    elif action == "stop":
        result = send_rpc(API_STOP_AI_CHAT, "", "stop_tts")
    else:
        print_json({
            "ok": False,
            "code": 400,
            "action": action,
            "error": "Unknown action",
        })
        return 1

    print_json(result)
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
