#!/usr/bin/env python3

import base64
import json
import os
import sys
import urllib.error
import urllib.request


DEFAULT_MODEL = "gpt-4.1-mini"
DEFAULT_BASE_URL = "https://api.openai.com/v1/responses"
OPENAI_KEY_NAMES = ("OPENAI_API_KEY", "CHATGPT_API_KEY", "CHAT_GPT_API", "API_KEY")


def print_json(payload):
    print(json.dumps(payload, ensure_ascii=True))


def resolve_api_key():
    for name in OPENAI_KEY_NAMES:
        value = os.environ.get(name, "").strip()
        if value:
            return value, name
    return "", ""


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


def analyze_image(prompt, image_path):
    api_key, key_name = resolve_api_key()
    if not api_key:
        return {
            "ok": False,
            "code": 503,
            "error": "Missing OpenAI API key",
        }

    if not os.path.exists(image_path):
        return {
            "ok": False,
            "code": 404,
            "error": f"Image not found: {image_path}",
        }

    model = os.environ.get("BOOSTER_OPENAI_VISION_MODEL", "").strip() or DEFAULT_MODEL
    with open(image_path, "rb") as image_file:
        encoded = base64.b64encode(image_file.read()).decode("ascii")

    request_body = {
        "model": model,
        "input": [
            {
                "role": "user",
                "content": [
                    {
                        "type": "input_text",
                        "text": prompt,
                    },
                    {
                        "type": "input_image",
                        "image_url": f"data:image/jpeg;base64,{encoded}",
                    },
                ],
            }
        ],
        "max_output_tokens": 160,
    }

    request = urllib.request.Request(
        DEFAULT_BASE_URL,
        data=json.dumps(request_body).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        return {
            "ok": False,
            "code": exc.code,
            "error": "OpenAI request failed",
            "response_body": exc.read().decode("utf-8", errors="replace"),
        }
    except Exception as exc:
        return {
            "ok": False,
            "code": 500,
            "error": f"OpenAI request error: {exc}",
        }

    answer = extract_output_text(payload)
    return {
        "ok": True,
        "code": 0,
        "model": model,
        "api_key_name": key_name,
        "answer": answer,
        "response_body": payload,
    }


def main():
    if len(sys.argv) != 3:
        print_json({
            "ok": False,
            "code": 400,
            "action": "usage",
            "error": "Usage: openai_vision.py <analyze> <json-payload>",
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

    if action != "analyze":
        print_json({
            "ok": False,
            "code": 400,
            "action": action,
            "error": "Unknown action",
        })
        return 1

    prompt = str(payload.get("prompt", "")).strip()
    image_path = str(payload.get("image_path", "")).strip()
    if not prompt or not image_path:
        print_json({
            "ok": False,
            "code": 400,
            "action": action,
            "error": "Missing prompt or image_path",
        })
        return 1

    result = analyze_image(prompt, image_path)
    result["action"] = action
    result["image_path"] = image_path
    print_json(result)
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
