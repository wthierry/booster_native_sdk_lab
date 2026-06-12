#!/usr/bin/env python3

import datetime as dt
import json
import os
import signal
import subprocess
import tempfile
import time


STATE_PATH = os.getenv("BOOSTER_ROBOT_JOINT_STATE_PATH", "/tmp/booster_robot_joint_state.json")
LOG_PATH = os.getenv("BOOSTER_ROBOT_JOINT_LOG_PATH", "/tmp/booster_robot_joint_state.log")
TOPIC = os.getenv("BOOSTER_ROBOT_JOINT_TOPIC", "/joint_states")
POLL_PERIOD_S = float(os.getenv("BOOSTER_ROBOT_JOINT_POLL_PERIOD_S", "0.75"))
ROS_SETUP = os.getenv("BOOSTER_ROBOT_ROS_SETUP", "/opt/ros/humble/setup.bash")
ROBOT_SETUP = os.getenv(
    "BOOSTER_ROBOT_SDK_SETUP",
    "/home/booster/Workspace/booster_robotics_sdk_ros2/install/setup.bash",
)
RUNNING = True


def iso_now():
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def log_line(message):
    log_dir = os.path.dirname(LOG_PATH)
    if log_dir:
        os.makedirs(log_dir, exist_ok=True)
    with open(LOG_PATH, "a", encoding="utf-8") as handle:
        handle.write(f"{iso_now()} {message}\n")


def write_state(payload):
    payload = dict(payload)
    payload["updated_at"] = iso_now()
    state_dir = os.path.dirname(STATE_PATH) or "."
    os.makedirs(state_dir, exist_ok=True)
    fd, tmp_path = tempfile.mkstemp(prefix=".booster_robot_joint_state.", suffix=".tmp", dir=state_dir)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, ensure_ascii=True, indent=2)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(tmp_path, STATE_PATH)
    finally:
        try:
            if os.path.exists(tmp_path):
                os.unlink(tmp_path)
        except OSError:
            pass


def set_state(state_data, **updates):
    state_data.update(updates)
    write_state(state_data)


def handle_signal(_signum, _frame):
    global RUNNING
    RUNNING = False


def parse_joint_state(text):
    result = {
        "stamp": {"sec": 0, "nanosec": 0},
        "names": [],
        "position": [],
        "velocity": [],
        "effort": [],
    }
    section = None
    in_stamp = False
    for raw_line in text.splitlines():
        line = raw_line.rstrip()
        stripped = line.strip()
        if not stripped or stripped == "---":
            continue
        if stripped == "header:":
            section = None
            in_stamp = False
            continue
        if stripped == "stamp:":
            in_stamp = True
            continue
        if in_stamp and stripped.startswith("sec:"):
            result["stamp"]["sec"] = int(stripped.split(":", 1)[1].strip() or 0)
            continue
        if in_stamp and stripped.startswith("nanosec:"):
            result["stamp"]["nanosec"] = int(stripped.split(":", 1)[1].strip() or 0)
            continue
        if stripped in ("name:", "position:", "velocity:", "effort:"):
            section = stripped[:-1]
            in_stamp = False
            continue
        if stripped.startswith("- ") and section:
            value = stripped[2:].strip()
            if section == "name":
                result["names"].append(value)
            else:
                result[section].append(float(value))
    if not result["names"]:
        raise ValueError("no joint names parsed")
    return result


def capture_joint_state():
    command = (
        f"source {json.dumps(ROS_SETUP)} >/dev/null 2>&1 && "
        f"source {json.dumps(ROBOT_SETUP)} >/dev/null 2>&1 && "
        f"timeout 3 ros2 topic echo {json.dumps(TOPIC)} --once"
    )
    proc = subprocess.run(
        ["bash", "-lc", command],
        capture_output=True,
        text=True,
        timeout=5,
        check=False,
    )
    if proc.returncode != 0:
        stderr = (proc.stderr or "").strip()
        stdout = (proc.stdout or "").strip()
        raise RuntimeError(stderr or stdout or f"ros2 topic echo exited with code {proc.returncode}")
    return parse_joint_state(proc.stdout)


def main():
    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)
    open(LOG_PATH, "w", encoding="utf-8").close()

    state = {
        "ok": True,
        "available": False,
        "running": True,
        "pid": os.getpid(),
        "state": "starting",
        "topic": TOPIC,
        "names": [],
        "position": [],
        "velocity": [],
        "effort": [],
        "joint_count": 0,
        "last_error": "",
    }
    write_state(state)
    log_line(f"polling topic={TOPIC}")

    while RUNNING:
        try:
            message = capture_joint_state()
            set_state(
                state,
                available=True,
                ok=True,
                state="streaming",
                last_error="",
                stamp=message["stamp"],
                names=message["names"],
                position=message["position"],
                velocity=message["velocity"],
                effort=message["effort"],
                joint_count=len(message["names"]),
            )
        except Exception as exc:
            set_state(
                state,
                available=False,
                ok=False,
                state="error",
                last_error=str(exc),
            )
            log_line(f"capture failed: {exc}")
        time.sleep(POLL_PERIOD_S)

    set_state(state, running=False, state="stopped")
    log_line("stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
