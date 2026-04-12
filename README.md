# Booster Native SDK Lab

## Robot Hardware Snapshot

Observed directly over SSH on `2026-04-10` from `booster@192.168.5.149`.

- Platform: Qualcomm QCS8550 development-kit-class board. The device tree reports `Qualcomm Technologies, Inc. Kalamap QCS8550 DK` with compatibles `qcom,kalamap-iot`, `qcom,kalamap`, and `qcom,iot`.
- OS: Ubuntu 22.04.2 LTS on `arm64`, kernel `5.15.153-qki-consolidate-android13-8-g412c9682f705-dirty`.
- CPU: 8-core heterogeneous ARMv9 cluster. From `/proc/cpuinfo` part IDs, this unit appears to be `3x Cortex-A510` up to `2.016 GHz`, `2x Cortex-A715` up to `2.803 GHz`, `2x Cortex-A710` up to `2.803 GHz`, and `1x Cortex-X3` up to `3.187 GHz`.
- GPU: `Adreno740v2` from `/sys/class/kgsl/kgsl-3d0/gpu_model`.
- Memory: `10 GiB` RAM visible to Linux, with `5.4 GiB` zram swap configured.
- Storage: SK hynix UFS device `HN8T05DEHKX073` on the Qualcomm UFS host (`ID_PATH=platform-1d84000.ufshc-scsi-0:0:0:0`). Main root partition is `115G`, with `/` currently mounted from `/dev/sda2` (`94G` filesystem, `47G` free at capture time). The partition layout also includes dedicated `/firmware` and `/dsp` mounts.
- Networking: `wlan0`, `eth0`, and `usbeth` are active on this robot. PCIe/USB enumeration shows multiple Gigabit Ethernet adapters, including Realtek `RTL8111/8168/8411` controllers and a Realtek `RTL8153` USB GbE adapter.
- Audio I/O: capture is through an `iFLYTEK XFM-DP-V0.0.18` USB microphone array (`16 kHz`, mono in PulseAudio), and playback is through a `C-Media USB Audio Device`.
- USB / expansion devices seen live: Renesas `uPD720201` USB 3.0 controller, the `XFM-DP-V0.0.18` mic array, the C-Media USB audio device, and a QinHeng USB serial device.
- Camera / video nodes: `/dev/video32` and `/dev/video33` exist on this unit, but the camera model is not identified by the currently installed userspace tools.
- Accelerator note: there is no NVIDIA stack on this robot (`nvidia-smi` is absent). Any local inference or ASR acceleration here needs to target Qualcomm CPU/GPU/NPU paths rather than CUDA.

Minimal C++ HTTP wrapper over the native Booster SDK in [`../booster_robotics_sdk`](../booster_robotics_sdk).

The primary supported speech path is the native LUI/RTC route, redirected locally through the fake ByteDance bridge in [`scripts/fake_bytedance_openai_asr.py`](scripts/fake_bytedance_openai_asr.py).

The repo still contains optional experimental ASR helpers for WhisperLive, Moonshine, and direct OpenAI transcription, but they are disabled by default and only exposed when `BOOSTER_ENABLE_EXPERIMENTAL_ASR=1` is set.

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

In macOS dev mode, robot-only features are mocked so the UI can run without the Booster SDK, ROS, DDS, or PulseAudio. Use `BOOSTER_CAMERA_PREVIEW_PATH` to point `/camera/preview.jpg` at a local image.

To expose the experimental non-native ASR controls in the UI and API, set:

```bash
BOOSTER_ENABLE_EXPERIMENTAL_ASR=1
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
