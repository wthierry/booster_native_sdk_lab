#!/usr/bin/env python3

import json
import os
import ssl
import sys
import urllib.error
import urllib.request
import uuid


OPENAI_KEY_NAMES = ("CHATGPT_API_KEY", "OPENAI_API_KEY", "CHAT_GPT_API", "API_KEY")
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REALTIME_CALLS_URL = "https://api.openai.com/v1/realtime/calls"


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


def resolve_api_key():
    for name in OPENAI_KEY_NAMES:
        value = os.environ.get(name, "").strip()
        if value:
            return value, name
    return "", ""


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


def encode_multipart_form(parts):
    boundary = f"----booster-{uuid.uuid4().hex}"
    body = bytearray()

    for name, content, content_type in parts:
        body.extend(f"--{boundary}\r\n".encode("utf-8"))
        body.extend(f'Content-Disposition: form-data; name="{name}"\r\n'.encode("utf-8"))
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


def main():
    load_dotenv_if_present()

    if len(sys.argv) != 3:
        print_json({
            "ok": False,
            "code": 400,
            "error": "Usage: openai_realtime_call.py <session-json> <offer-sdp-file>",
        })
        return 1

    try:
        session = json.loads(sys.argv[1])
    except Exception as exc:
        print_json({
            "ok": False,
            "code": 400,
            "error": f"Invalid session JSON: {exc}",
        })
        return 1

    try:
        with open(sys.argv[2], "r", encoding="utf-8") as handle:
            offer_sdp = handle.read()
    except Exception as exc:
        print_json({
            "ok": False,
            "code": 400,
            "error": f"Unable to read SDP file: {exc}",
        })
        return 1

    api_key, key_name = resolve_api_key()
    if not api_key:
        print_json({
            "ok": False,
            "code": 503,
            "error": "Missing OpenAI API key",
        })
        return 1

    boundary, body = encode_multipart_form([
        ("sdp", offer_sdp, "application/sdp"),
        ("session", json.dumps(session), "application/json"),
    ])

    request = urllib.request.Request(
        REALTIME_CALLS_URL,
        data=body,
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": f"multipart/form-data; boundary={boundary}",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=30, context=build_ssl_context()) as response:
            sdp_answer = response.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        print_json({
            "ok": False,
            "code": exc.code,
            "error": "OpenAI Realtime call failed",
            "response_body": exc.read().decode("utf-8", errors="replace"),
        })
        return 1
    except Exception as exc:
        print_json({
            "ok": False,
            "code": 500,
            "error": f"OpenAI Realtime request error: {exc}",
        })
        return 1

    print_json({
        "ok": True,
        "code": 0,
        "api_key_name": key_name,
        "sdp_answer": sdp_answer,
    })
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
