#!/usr/bin/env python3
import fcntl
import os
import socket
import struct
import sys


def raise_loopback():
    control = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    request = struct.pack("16sH14x", b"lo", 0)
    flags = struct.unpack("16sH14x", fcntl.ioctl(control, 0x8913, request))[1]
    request = struct.pack("16sH14x", b"lo", flags | 1)
    fcntl.ioctl(control, 0x8914, request)


def connect(family, address):
    listener = socket.socket(family, socket.SOCK_STREAM)
    listener.bind((address, 0))
    listener.listen(1)
    client = socket.socket(family, socket.SOCK_STREAM)
    client.connect(listener.getsockname()[:2])
    accepted, _ = listener.accept()
    server_port = listener.getsockname()[1]
    client_port = client.getsockname()[1]
    return [listener, client, accepted], [
        server_port,
        client_port,
        os.fstat(accepted.fileno()).st_ino,
        os.fstat(client.fileno()).st_ino,
    ]


if os.environ.get("LOOPBACK_UP") == "1":
    raise_loopback()

held, ipv4 = connect(socket.AF_INET, "127.0.0.1")
ipv6 = ["-", "-", "-", "-"]
try:
    more, ipv6 = connect(socket.AF_INET6, "::1")
    held += more
except OSError:
    pass

print("READY", os.getpid(), *ipv4, *ipv6, flush=True)
sys.stdin.read(1)
