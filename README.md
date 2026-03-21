# Booster Native SDK Lab

Minimal C++ HTTP wrapper over the native Booster SDK in [`../booster_robotics_sdk`](../booster_robotics_sdk).

## What It Exposes

- `GET /health`
- `GET /battery`

The wrapper talks directly to the native SDK battery subscriber and serves a browser UI from `/`.

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

By default the wrapper binds to `0.0.0.0:8080` and uses `lo`.

```bash
./build/booster_sdk_http_wrapper
```

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

## Example Calls

Health:

```bash
curl http://127.0.0.1:8080/health
```

Battery:

```bash
curl http://127.0.0.1:8080/battery
```
