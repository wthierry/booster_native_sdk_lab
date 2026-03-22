#!/usr/bin/env python3

import os
import sys

import rclpy
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy
from sensor_msgs.msg import CompressedImage


TOPIC_NAME = "/booster_video_stream"


class FrameCache:
    def __init__(self, node, output_path):
        self.output_path = output_path
        qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=1,
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.VOLATILE,
        )
        self.subscription = node.create_subscription(
            CompressedImage,
            TOPIC_NAME,
            self.handle_image,
            qos,
        )

    def handle_image(self, msg):
        temp_path = f"{self.output_path}.tmp"
        with open(temp_path, "wb") as image_file:
            image_file.write(bytes(msg.data))
        os.replace(temp_path, self.output_path)


def main():
    if len(sys.argv) != 2:
        print("Usage: ros_camera_frame.py <output-path>", file=sys.stderr)
        return 1

    output_path = sys.argv[1]
    rclpy.init()
    node = rclpy.create_node("booster_native_sdk_lab_camera_cache")
    FrameCache(node, output_path)

    try:
        rclpy.spin(node)
        return 0
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
