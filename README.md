# Booster Native SDK Lab

Minimal C++ HTTP wrapper over the native Booster SDK in [`../booster_robotics_sdk`](../booster_robotics_sdk).

## What It Exposes

- `GET /health`
- `GET /battery`

The wrapper talks directly to the native SDK battery subscriber and serves a browser UI from `/`.

## Build

Robot/Linux build:

```bash
cmake -S . -B build
cmake --build build -j
```

macOS dev build:

```bash
cmake -S . -B build -DBOOSTER_DEV_MODE=ON
cmake --build build -j
```

## Run

By default the wrapper binds to `0.0.0.0:8080` and uses `lo`.

```bash
./build/booster_sdk_http_wrapper
```

On macOS, the wrapper auto-loads `.env` from the repo root. On the robot, [`booster-native-sdk-lab.service`](booster-native-sdk-lab.service) already loads the same file via `EnvironmentFile=`.

Then open:

```bash
http://127.0.0.1:8080/
```

Or from another machine:

```bash
http://ROBOT_IP:8080/
```

Override the interface if needed:

```bash
./build/booster_sdk_http_wrapper --iface eth0 --port 8081
```

Or:

```bash
BOOSTER_NETWORK_INTERFACE=lo BOOSTER_PORT=8080 ./build/booster_sdk_http_wrapper
```

In macOS dev mode, robot-only features are mocked so the UI can run without the Booster SDK, ROS, DDS, or PulseAudio. Use `BOOSTER_CAMERA_PREVIEW_PATH` to point `/camera/preview.jpg` and OpenAI vision at a local image.

For the fastest local Mac voice path, the UI can open an OpenAI Realtime WebRTC session using your server-side API key. Supported env vars:

```bash
CHATGPT_API_KEY=...
BOOSTER_OPENAI_REALTIME_MODEL=gpt-realtime
BOOSTER_OPENAI_REALTIME_VOICE=verse
BOOSTER_OPENAI_REALTIME_INSTRUCTIONS="You are Booster. Reply in one short sentence."
```

The Mac UI also exposes an OpenAI text mode. Optional text env vars:

```bash
BOOSTER_OPENAI_TEXT_MODEL=gpt-4.1-mini
BOOSTER_OPENAI_TEXT_SYSTEM_PROMPT="You are Booster. Reply clearly and briefly."
```

## Example Calls

Health:

```bash
curl http://127.0.0.1:8080/health
```

Battery:

```bash
curl http://127.0.0.1:8080/battery
```
