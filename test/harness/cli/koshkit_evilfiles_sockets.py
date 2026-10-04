#!/usr/bin/env python3
"""Socket fidelity, ss parity, and terminal width probes for evilfiles, evilss,
and evilps. The process holds a pipe, a socketpair, a named Unix socket, and
loopback TCP connections, then lists its own descriptors with the shell."""

import fcntl
import os
import pty
import re
import select
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import termios
import time
import unicodedata


BIN = os.environ["BIN"]
WORK = os.environ["TEST_TEMP_DIRECTORY"]
ESCAPE = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][A-Za-z0-9]")


def run_shell(command, timeout=20):
    result = subprocess.run([BIN, "-Q", "-c", command], capture_output=True,
                            timeout=timeout)
    return result.returncode, result.stdout.decode("utf-8", "replace")


def normalize(text, socket_path):
    text = text.replace(socket_path, "SOCKPATH")
    text = re.sub(r"(?<=:)\d{4,5}\b", "PORT", text)
    text = re.sub(r"(?<=->)\d+(?![\d.])", "N", text)
    return text


def describe_descriptors(shell_pid, descriptors, socket_path):
    code, output = run_shell(
        "koshkit --color never evilfiles -w -p %d" % shell_pid)
    if code != 0:
        return ["evilfiles-status=%d" % code]
    row_pattern = re.compile(
        r"^\s*\S+\s+\d+\s+\S+\s+(\d+)[a-z]\s+(\S+)\s+(\S+)\s+(\S+)\s+\S+"
        r"\s+\d+\s+\d+\s+(\d+)\s+(.*?)\s*(?:socket|pipe):\[\d+\]$")
    by_inode = {}
    for line in output.splitlines():
        match = row_pattern.match(line)
        if match:
            by_inode[int(match.group(5))] = match
    lines = []
    for label, descriptor in descriptors:
        inode = os.fstat(descriptor).st_ino
        match = by_inode.get(inode)
        if match is None:
            lines.append("%s: missing" % label)
            continue
        endpoint = normalize(match.group(6), socket_path)
        lines.append("%s: TYPE=%s MODE=%s ENDPOINT=%s" %
                     (label, match.group(2), match.group(3), endpoint))
    return lines


def unix_socket_lines(text, inodes):
    lines = []
    for line in text.splitlines():
        fields = line.split()
        if len(fields) != 8:
            continue
        if int(fields[5]) in inodes:
            lines.append(" ".join(fields))
    return sorted(lines)


def compare_unix_sockets(inodes):
    if shutil.which("ss") is None:
        return "ss-parity=skipped-no-ss"
    reference = subprocess.run(["ss", "-xaH"], capture_output=True)
    if reference.returncode != 0:
        return "ss-parity=skipped-ss-failed"
    code, output = run_shell("koshkit --color never evilss -xaH")
    if code != 0:
        return "ss-parity=evilss-status-%d" % code
    ours = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) != 6:
            continue
        for index in (4, 5):
            head, _, tail = fields[index].rpartition(":")
            fields[index] = head
            fields.append(tail)
        ours.append(" ".join([fields[0], fields[1], fields[2], fields[3],
                              fields[4], fields[6], fields[5], fields[7]]))
    expected = unix_socket_lines(reference.stdout.decode(), inodes)
    actual = unix_socket_lines("\n".join(ours), inodes)
    if expected == actual and expected:
        return "ss-parity=match"
    sys.stderr.write("ss:\n%s\nevilss:\n%s\n" %
                     ("\n".join(expected), "\n".join(actual)))
    return "ss-parity=mismatch"


def display_width(text):
    width = 0
    for character in text:
        if unicodedata.combining(character):
            continue
        width += 2 if unicodedata.east_asian_width(character) in "WF" else 1
    return width


def run_pty(command, columns, rows, live=False):
    pid, descriptor = pty.fork()
    if pid == 0:
        os.execv(BIN, [BIN, "-Q", "-c", command])
    fcntl.ioctl(descriptor, termios.TIOCSWINSZ,
                struct.pack("HHHH", rows, columns, 0, 0))
    output = bytearray()
    deadline = time.monotonic() + (1.5 if live else 15.0)
    while time.monotonic() < deadline:
        ready, _, _ = select.select([descriptor], [], [], 0.05)
        if not ready:
            continue
        try:
            chunk = os.read(descriptor, 65536)
        except OSError:
            break
        if not chunk:
            break
        output.extend(chunk)
    if live:
        os.kill(pid, signal.SIGINT)
        drain = time.monotonic() + 1.0
        while time.monotonic() < drain:
            ready, _, _ = select.select([descriptor], [], [], 0.05)
            if not ready:
                continue
            try:
                chunk = os.read(descriptor, 65536)
            except OSError:
                break
            if not chunk:
                break
            output.extend(chunk)
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    text = ESCAPE.sub(b"", bytes(output)).decode("utf-8", "replace")
    return [line for line in text.replace("\r", "").split("\n") if line]


def summarize_width(label, lines, columns, needle):
    holder_lines = [line for line in lines if needle in line]
    widest = max([display_width(line) for line in lines] or [0])
    has_ellipsis = any(line.rstrip().endswith("...") for line in holder_lines)
    print("%s: rows=%s fits=%s ellipsis=%s" %
          (label, "yes" if holder_lines else "no",
           "yes" if widest <= columns else "no",
           "yes" if has_ellipsis else "no"))


def width_probes(long_name, helper_pid):
    files = "koshkit --color never evilfiles -p %d" % helper_pid
    processes = "koshkit --color never evilps -A %d" % helper_pid
    for columns in (150, 130):
        lines = run_pty(files, columns, 30)
        summarize_width("evilfiles-narrow-%d" % columns, lines, columns,
                        "python3")
    lines = run_pty(files, 400, 30)
    summarize_width("evilfiles-wide-terminal", lines, 400, "python3")
    print("evilfiles-wide-terminal-whole: %s" %
          ("yes" if any(long_name in line for line in lines) else "no"))
    lines = run_pty(files + " -w", 130, 30)
    print("evilfiles-wide-flag-whole: %s" %
          ("yes" if any(long_name in line for line in lines) else "no"))

    for columns in (60, 40):
        lines = run_pty(processes, columns, 30)
        summarize_width("evilps-narrow-%d" % columns, lines, columns,
                        "python3")
    lines = run_pty(processes, 400, 30)
    print("evilps-wide-terminal-whole: %s" %
          ("yes" if any(long_name in line for line in lines) else "no"))
    lines = run_pty(processes + " -w", 60, 30)
    print("evilps-wide-flag-whole: %s" %
          ("yes" if any(long_name in line for line in lines) else "no"))
    lines = run_pty("koshkit --color never evilps -A -l 0.2 %d" % helper_pid,
                    60, 30, live=True)
    holder_lines = [line for line in lines if "python3" in line]
    widest = max([display_width(line) for line in lines] or [0])
    print("evilps-live-narrow: rows=%s fits=%s" %
          ("yes" if holder_lines else "no", "yes" if widest <= 60 else "no"))
    lines = run_pty("koshkit --color never evilps -A -l 0.2 -w %d" %
                    helper_pid, 60, 30, live=True)
    print("evilps-live-wide-flag-whole: %s" %
          ("yes" if any(long_name in line for line in lines) else "no"))


def main():
    socket_path = os.path.join(WORK, "evilfiles-sockets.sock")
    if os.path.exists(socket_path):
        os.unlink(socket_path)
    wide_text = "".join(chr(code) for code in (0x65e5, 0x672c, 0x8a9e))
    long_name = "wide-" + wide_text * 12 + "-" + chr(0xe9) * 3 + "-tail"
    long_path = os.path.join(WORK, long_name)
    held_file = open(long_path, "w")

    read_end, write_end = os.pipe()
    pair_left, pair_right = socket.socketpair()
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    listener.bind(socket_path)
    listener.listen(1)
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.connect(socket_path)
    server, _ = listener.accept()
    tcp_listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp_listener.bind(("127.0.0.1", 0))
    tcp_listener.listen(1)
    tcp_client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp_client.connect(tcp_listener.getsockname())
    tcp_server, _ = tcp_listener.accept()
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.bind(("127.0.0.1", 0))

    descriptors = [
        ("pipe-read", read_end),
        ("pipe-write", write_end),
        ("socketpair", pair_left.fileno()),
        ("unix-listener", listener.fileno()),
        ("unix-client", client.fileno()),
        ("unix-accepted", server.fileno()),
        ("tcp-listener", tcp_listener.fileno()),
        ("tcp-client", tcp_client.fileno()),
        ("tcp-accepted", tcp_server.fileno()),
        ("udp", udp.fileno()),
    ]
    try:
        v6 = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        v6.bind(("::1", 0))
        v6.listen(1)
        descriptors.append(("tcp6-listener", v6.fileno()))
    except OSError:
        print("tcp6-listener: skipped-no-ipv6")

    helper = subprocess.Popen(
        [sys.executable, "-c",
         "import sys,time;f=open(sys.argv[1]);print('ready',flush=True);"
         "time.sleep(30)", long_path],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    helper.stdout.readline()

    for line in describe_descriptors(os.getpid(), descriptors, socket_path):
        print(line)

    unix_inodes = {os.fstat(descriptor.fileno()).st_ino for descriptor in
                   (pair_left, pair_right, listener, client, server)}
    print(compare_unix_sockets(unix_inodes))

    width_probes(long_name, helper.pid)

    helper.kill()
    helper.wait()
    held_file.close()
    os.unlink(long_path)
    os.unlink(socket_path)


main()
