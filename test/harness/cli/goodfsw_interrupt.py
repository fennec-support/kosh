#!/usr/bin/env python3

import os
import select
import signal
import subprocess
import time


root = os.environ["INTERRUPT_ROOT"]
process = subprocess.Popen(
    [
        os.environ["BIN"],
        "-c",
        "koshkit goodfsw -r -l 0.05 " + root,
    ],
    stdout=subprocess.PIPE,
    stderr=subprocess.DEVNULL,
)

deadline = time.monotonic() + 10
is_watching = False
touch_count = 0
while not is_watching and time.monotonic() < deadline:
    touch_count += 1
    with open(os.path.join(root, "touched"), "w") as handle:
        handle.write(str(touch_count))
    readable, _, _ = select.select([process.stdout], [], [], 0.1)
    is_watching = bool(readable)

process.send_signal(signal.SIGINT)
try:
    process.communicate(timeout=10)
    status = process.returncode
except subprocess.TimeoutExpired:
    process.kill()
    process.communicate()
    status = 124
print(status if is_watching else "never-watched")
