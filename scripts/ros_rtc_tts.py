#!/usr/bin/env python3

import json
import sys
import time

import rclpy
from booster_interface.srv import RpcService


WELCOME_MSG = "Welcome! It's great to see you"
SYSTEM_PROMPT = """
## prompt
You're a robot named Adam that can offer services like emotional chatting to users.
Always respond in English.

## skill
I am a professional chatbot capable of providing conversation services with a variety of voices.
""".strip()

API_START_AI_CHAT = 2000
API_STOP_AI_CHAT = 2001
API_SPEAK = 2002
SERVICE_NAME = "booster_rtc_service"


def print_json(payload):
    print(json.dumps(payload, ensure_ascii=True))


def send_rpc(node, client, api_id, body, action):
    if not client.wait_for_service(timeout_sec=2.0):
        return {
            "ok": False,
            "code": 503,
            "action": action,
            "error": "ROS RTC service not available",
        }

    request = RpcService.Request()
    request.msg.api_id = api_id
    request.msg.body = body

    future = client.call_async(request)
    deadline = time.time() + 10.0
    while rclpy.ok() and not future.done() and time.time() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)

    if not future.done():
        return {
            "ok": False,
            "code": 504,
            "action": action,
            "error": "ROS RTC service request timed out",
        }

    response = future.result().msg
    payload = {
        "ok": response.status == 0,
        "code": int(response.status),
        "action": action,
    }
    if response.body:
        try:
            payload["response_body"] = json.loads(response.body)
        except Exception:
            payload["response_body"] = response.body
    return payload


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

    rclpy.init()
    node = rclpy.create_node("booster_native_sdk_lab_tts_helper")
    client = node.create_client(RpcService, SERVICE_NAME)

    try:
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
            result = send_rpc(node, client, API_START_AI_CHAT, body, "start_tts")
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
            result = send_rpc(node, client, API_SPEAK, json.dumps({"msg": text}), "speak_tts")
            result["text"] = text
        elif action == "stop":
            result = send_rpc(node, client, API_STOP_AI_CHAT, "", "stop_tts")
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
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
