#!/usr/bin/env python3

import argparse
import asyncio
import os
import signal
import socket
import sys
import time


def parse_args():
    parser = argparse.ArgumentParser(description="Transparent TCP relay for openspeech interception")
    parser.add_argument("--listen-host", default="127.0.0.1")
    parser.add_argument("--listen-port", type=int, default=443)
    parser.add_argument("--upstream-host", required=True)
    parser.add_argument("--upstream-port", type=int, default=443)
    parser.add_argument("--upstream-ip", action="append", dest="upstream_ips", default=[])
    parser.add_argument("--log-path", default="/var/log/openspeech_tcp_relay.log")
    parser.add_argument("--pid-path", default="/run/openspeech_tcp_relay.pid")
    return parser.parse_args()


class RelayServer:
    def __init__(self, args):
        self.args = args
        self.server = None
        self.ip_index = 0
        self.stopping = False

    def log(self, message):
        stamp = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        line = f"{stamp} {message}\n"
        with open(self.args.log_path, "a", encoding="utf-8") as handle:
            handle.write(line)

    def choose_upstream_ip(self):
        if not self.args.upstream_ips:
            raise RuntimeError("no upstream IPs configured")
        ip = self.args.upstream_ips[self.ip_index % len(self.args.upstream_ips)]
        self.ip_index += 1
        return ip

    async def pump(self, reader, writer, label):
        transferred = 0
        try:
            while True:
                chunk = await reader.read(65536)
                if not chunk:
                    break
                transferred += len(chunk)
                writer.write(chunk)
                await writer.drain()
        except Exception as exc:
            self.log(f"{label} pump error: {exc}")
        finally:
            try:
                writer.close()
                await writer.wait_closed()
            except Exception:
                pass
        return transferred

    async def handle_client(self, client_reader, client_writer):
        peer = client_writer.get_extra_info("peername")
        upstream_ip = self.choose_upstream_ip()
        self.log(f"accepted client={peer} upstream={upstream_ip}:{self.args.upstream_port}")
        try:
            upstream_reader, upstream_writer = await asyncio.open_connection(
                host=upstream_ip,
                port=self.args.upstream_port,
                ssl=False,
                family=socket.AF_INET,
            )
        except Exception as exc:
            self.log(f"connect failed client={peer} upstream={upstream_ip}:{self.args.upstream_port} error={exc}")
            try:
                client_writer.close()
                await client_writer.wait_closed()
            except Exception:
                pass
            return

        upstream_label = f"client->{upstream_ip}"
        downstream_label = f"{upstream_ip}->client"
        upload_task = asyncio.create_task(self.pump(client_reader, upstream_writer, upstream_label))
        download_task = asyncio.create_task(self.pump(upstream_reader, client_writer, downstream_label))
        done, pending = await asyncio.wait(
            {upload_task, download_task},
            return_when=asyncio.FIRST_COMPLETED,
        )
        for task in pending:
            task.cancel()
        uploaded = 0
        downloaded = 0
        for task in done:
            try:
                result = await task
            except Exception:
                result = 0
            if task is upload_task:
                uploaded = result
            elif task is download_task:
                downloaded = result
        self.log(f"closed client={peer} upstream={upstream_ip} uploaded={uploaded} downloaded={downloaded}")

    async def start(self):
        self.server = await asyncio.start_server(
            self.handle_client,
            host=self.args.listen_host,
            port=self.args.listen_port,
            reuse_address=True,
        )
        self.log(
            f"listening on {self.args.listen_host}:{self.args.listen_port} -> "
            f"{self.args.upstream_host}:{self.args.upstream_port} ips={','.join(self.args.upstream_ips)}"
        )
        with open(self.args.pid_path, "w", encoding="utf-8") as handle:
            handle.write(f"{os.getpid()}\n")
        async with self.server:
            await self.server.serve_forever()

    async def stop(self):
        if self.stopping:
            return
        self.stopping = True
        self.log("stopping relay")
        if self.server is not None:
            self.server.close()
            await self.server.wait_closed()
        try:
            os.unlink(self.args.pid_path)
        except OSError:
            pass


async def main_async():
    args = parse_args()
    relay = RelayServer(args)
    loop = asyncio.get_running_loop()
    stop_event = asyncio.Event()

    def request_stop():
        stop_event.set()

    for signum in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(signum, request_stop)

    server_task = asyncio.create_task(relay.start())
    stop_task = asyncio.create_task(stop_event.wait())
    done, pending = await asyncio.wait({server_task, stop_task}, return_when=asyncio.FIRST_COMPLETED)
    if stop_task in done:
        await relay.stop()
        server_task.cancel()
        try:
            await server_task
        except asyncio.CancelledError:
            pass
        return 0
    try:
        await server_task
    except asyncio.CancelledError:
        return 0
    return 0


def main():
    try:
        return asyncio.run(main_async())
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
