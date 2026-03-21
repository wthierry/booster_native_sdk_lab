#!/usr/bin/env python3

import json
import re
import subprocess
import sys


WELCOME_MSG = "Hello."
SYSTEM_PROMPT = """
## prompt
You're a robot named Booster that offers emotional chatting to users with a dry, sarcastic sense of humor.
Always respond in English.
Be witty, teasing, and lightly deadpan while still being genuinely helpful.
Keep the sarcasm playful and clever, not cruel or bullying.
Keep responses to 1 or 2 sentences when possible.
Only give a longer, more detailed answer when the user is clearly asking a detailed question or requests more depth.


## skill
I am a professional chatbot capable of providing conversation services with a variety of voices.
My style is clever, mildly sarcastic, and entertaining.
""".strip()

API_START_AI_CHAT = 2000
API_STOP_AI_CHAT = 2001
API_SPEAK = 2002
SERVICE_NAME = "booster_rtc_service"
SERVICE_TYPE = "booster_interface/srv/RpcService"


def print_json(payload):
    print(json.dumps(payload, ensure_ascii=True))


def parse_response(output):
    status_match = re.search(r"status=(\d+)", output)
    body_match = re.search(r"body='((?:\\'|[^'])*)'", output, re.DOTALL)
    if not status_match:
        return None

    status = int(status_match.group(1))
    body = ""
    if body_match:
        body = body_match.group(1).replace("\\'", "'")

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
            "error": "Usage: ros_rtc_tts.py <start|speak|stop> <json-payload>",
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
        voice_type = payload.get("voice_type", "zh_female_shuangkuaisisi_emo_v2_mars_bigtts")
        body = json.dumps({
            "interrupt_mode": True,
            "asr_config": {
                "interrupt_speech_duration": 200,
                "interrupt_keywords": ["stop", "shut up"],
            },
            "llm_config": {
                "system_prompt": SYSTEM_PROMPT,
                "welcome_msg": WELCOME_MSG,
                "prompt_name": "",
            },
            "tts_config": {
                "voice_type": voice_type,
                "ignore_bracket_text": [3],
            },
            "enable_face_tracking": False,
        })
        result = send_rpc(API_START_AI_CHAT, body, "start_tts")
        result["voice_type"] = voice_type
    elif action == "speak":
        text = str(payload.get("text", "")).strip()
        if not text:
            print_json({
                "ok": False,
                "code": 400,
                "action": "speak_tts",
                "error": "Missing text",
            })
            return 1
        result = send_rpc(API_SPEAK, json.dumps({"msg": text}), "speak_tts")
        result["text"] = text
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
