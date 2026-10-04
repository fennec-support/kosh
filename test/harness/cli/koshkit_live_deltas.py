#!/usr/bin/env python3
"""Real-I/O probes: evilio and evilnet report nonzero window deltas."""

import os
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time


FRAME_MARKER = "  LIVE  "


def run_report(binary, command):
    result = subprocess.run([binary, "-Q", "-c", command],
                            capture_output=True, text=True, timeout=20)
    return result.returncode, result.stdout


def run_live_report(binary, command, frame_count):
    with tempfile.TemporaryFile() as output:
        process = subprocess.Popen([binary, "-Q", "-c", command],
                                   stdout=output, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 15.0
        text = ""
        while time.monotonic() < deadline:
            time.sleep(0.05)
            output.seek(0)
            text = output.read().decode("utf-8", "replace")
            if text.count(FRAME_MARKER) >= frame_count + 1:
                break
        process.send_signal(signal.SIGINT)
        status = process.wait(timeout=5.0)
        output.seek(0)
        text = output.read().decode("utf-8", "replace")
    return status, text


def find_row(report, first_token):
    for line in report.splitlines():
        tokens = line.split()
        if tokens and tokens[0] == first_token:
            return tokens
    return None


def is_nonzero_count(token):
    return token.isdigit() and int(token) > 0


def is_nonzero_size(token):
    return token != "0" and token != "-"


def describe_process_report(report, pid, heading):
    row = find_row(report, str(pid))
    return {
        "heading": heading in report,
        "row": row is not None,
        "write-ops": "nonzero" if (
            row is not None and len(row) > 5
            and is_nonzero_count(row[4])) else "zero",
    }


def describe_network_report(report, heading):
    row = find_row(report, "lo")
    return {
        "heading": heading in report,
        "row": row is not None,
        "rx": "nonzero" if (
            row is not None and len(row) > 4
            and is_nonzero_size(row[1])) else "zero",
        "tx": "nonzero" if (
            row is not None and len(row) > 4
            and is_nonzero_size(row[2])) else "zero",
        "packets": "nonzero" if (
            row is not None and len(row) > 4
            and is_nonzero_count(row[3])
            and is_nonzero_count(row[4])) else "zero",
    }


def print_result(name, status, values):
    parts = ["status=%s" % status]
    parts.extend("%s=%s" % (key, value) for key, value in values.items())
    print("%s %s" % (name, " ".join(parts)))


def probe_evilio(binary):
    writer = subprocess.Popen(
        [sys.executable, "-c",
         "import os\nfd = os.open(os.devnull, os.O_WRONLY)\n"
         "while True:\n    os.write(fd, b'x')\n"])
    try:
        time.sleep(0.3)
        command = ("koshkit --color never evilio --cumulative=0.2 -p %d"
                   % writer.pid)
        status, report = run_report(binary, command)
        print_result("evilio-cumulative", status,
                     describe_process_report(report, writer.pid, "WRITE OPS/0.2s"))

        command = ("koshkit --color never evilio --live=0.1 "
                   "--cumulative=0.2 -p %d" % writer.pid)
        status, report = run_live_report(binary, command, 4)
        frame = report.split(FRAME_MARKER)[-1]
        print_result("evilio-live", status,
                     describe_process_report(frame, writer.pid, "WRITE OPS/0.2s"))
    finally:
        writer.kill()
        writer.wait()


def probe_evilnet(binary):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    port = listener.getsockname()[1]
    is_stopped = threading.Event()

    def consume():
        connection, _ = listener.accept()
        with connection:
            while not is_stopped.is_set():
                if not connection.recv(65536):
                    break

    def produce():
        chunk = b"x" * 65536
        with socket.create_connection(("127.0.0.1", port)) as connection:
            while not is_stopped.is_set():
                connection.sendall(chunk)

    consumer = threading.Thread(target=consume)
    producer = threading.Thread(target=produce)
    consumer.start()
    producer.start()
    try:
        time.sleep(0.3)
        status, report = run_report(
            binary, "koshkit --color never evilnet --traffic "
            "--cumulative=0.3")
        print_result("evilnet-cumulative", status,
                     describe_network_report(report, "RX/0.3s"))

        status, report = run_live_report(
            binary, "koshkit --color never evilnet --traffic --live=0.1 "
            "--cumulative=0.3", 4)
        frame = report.split(FRAME_MARKER)[-1]
        print_result("evilnet-live", status,
                     describe_network_report(frame, "RX/0.3s"))
    finally:
        is_stopped.set()
        listener.close()
        producer.join(timeout=5.0)
        consumer.join(timeout=5.0)


def main():
    if sys.platform != "linux":
        print("live delta probes: skipped (requires Linux)")
        return 0
    binary = os.environ.get("BIN")
    if not binary:
        print("BIN is required", file=sys.stderr)
        return 2

    probe_evilio(binary)
    probe_evilnet(binary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
