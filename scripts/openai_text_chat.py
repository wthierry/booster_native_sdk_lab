#!/usr/bin/env python3

import json
import os
import ssl
import sys
import urllib.error
import urllib.request


DEFAULT_MODEL = "gpt-4.1-mini"
DEFAULT_BASE_URL = "https://api.openai.com/v1/responses"
OPENAI_KEY_NAMES = ("CHATGPT_API_KEY", "OPENAI_API_KEY", "CHAT_GPT_API", "API_KEY")
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def print_json(payload):
    print(json.dumps(payload, ensure_ascii=True))


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


def respond_text(prompt, config):
    api_key, key_name = resolve_api_key()
    if not api_key:
        return {
            "ok": False,
            "code": 503,
            "error": "Missing OpenAI API key",
        }

    model = str(config.get("model", "")).strip() or DEFAULT_MODEL
    system_prompt = str(config.get("system_prompt", "")).strip()

    request_body = {
        "model": model,
        "input": [],
        "max_output_tokens": 220,
    }
    if system_prompt:
        request_body["input"].append({
            "role": "system",
            "content": [{"type": "input_text", "text": system_prompt}],
        })
    request_body["input"].append({
        "role": "user",
        "content": [{"type": "input_text", "text": prompt}],
    })

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
        with urllib.request.urlopen(request, timeout=25, context=build_ssl_context()) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        return {
            "ok": False,
            "code": exc.code,
            "error": "OpenAI text request failed",
            "response_body": exc.read().decode("utf-8", errors="replace"),
        }
    except Exception as exc:
        return {
            "ok": False,
            "code": 500,
            "error": f"OpenAI text request error: {exc}",
        }

    return {
        "ok": True,
        "code": 0,
        "model": model,
        "api_key_name": key_name,
        "answer": extract_output_text(payload),
        "response_body": payload,
    }


def main():
    load_dotenv_if_present()

    if len(sys.argv) != 3:
        print_json({
            "ok": False,
            "code": 400,
            "error": "Usage: openai_text_chat.py <config-json> <payload-json>",
        })
        return 1

    try:
        config = json.loads(sys.argv[1])
        payload = json.loads(sys.argv[2])
    except Exception as exc:
        print_json({
            "ok": False,
            "code": 400,
            "error": f"Invalid JSON: {exc}",
        })
        return 1

    prompt = str(payload.get("text", "")).strip()
    if not prompt:
        print_json({
            "ok": False,
            "code": 400,
            "error": "Missing text",
        })
        return 1

    result = respond_text(prompt, config)
    result["text"] = prompt
    print_json(result)
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
