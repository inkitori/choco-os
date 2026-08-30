#!/usr/bin/env python3
"""Drive choco-os in headless QEMU for automated testing.

Boots the ISO with the HMP monitor on a unix socket, serial to a file,
and exposes simple actions: type text, press keys, screendump, wait.

Usage examples:
  python3 tools/qemu_driver.py --boot-wait 12 --shot boot.png
  python3 tools/qemu_driver.py --boot-wait 12 --type "help" --key ret --wait 2 --shot help.png
  python3 tools/qemu_driver.py --boot-wait 12 --script "type help; key ret; wait 2; shot help.png"
"""
import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.path.join(REPO, "template.iso")

# Map chars to qemu sendkey names
KEYMAP = {
    " ": "spc", "-": "minus", "=": "equal", "[": "bracket_left",
    "]": "bracket_right", ";": "semicolon", "'": "apostrophe",
    "`": "grave_accent", "\\": "backslash", ",": "comma",
    ".": "dot", "/": "slash",
}
SHIFTMAP = {
    "!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6",
    "&": "7", "*": "8", "(": "9", ")": "0", "_": "minus", "+": "equal",
    "{": "bracket_left", "}": "bracket_right", ":": "semicolon",
    '"': "apostrophe", "~": "grave_accent", "|": "backslash",
    "<": "comma", ">": "dot", "?": "slash",
}


class Qemu:
    def __init__(self, iso=ISO, mem="4G", serial_log=None, extra=None):
        self.tmpdir = tempfile.mkdtemp(prefix="chocoqemu")
        self.mon_path = os.path.join(self.tmpdir, "mon.sock")
        self.serial_log = serial_log or os.path.join(self.tmpdir, "serial.log")
        cmd = [
            "qemu-system-x86_64", "-M", "q35", "-m", mem,
            "-cdrom", iso, "-boot", "d",
            "-display", "none",
            "-monitor", f"unix:{self.mon_path},server,nowait",
            "-serial", f"file:{self.serial_log}",
            "-device", "e1000,netdev=n0", "-netdev", "user,id=n0",
        ]
        if extra:
            cmd += extra
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL)
        # wait for monitor socket
        for _ in range(100):
            if os.path.exists(self.mon_path):
                break
            time.sleep(0.1)
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(self.mon_path)
        self.sock.settimeout(2)
        self._drain()

    def _drain(self):
        try:
            while True:
                data = self.sock.recv(65536)
                if not data:
                    break
        except socket.timeout:
            pass

    def cmd(self, line):
        self.sock.sendall((line + "\n").encode())
        time.sleep(0.05)
        self._drain()

    def key(self, name):
        self.cmd(f"sendkey {name}")
        time.sleep(0.06)

    def type(self, text):
        for ch in text:
            if ch.isupper():
                self.key(f"shift-{ch.lower()}")
            elif ch in SHIFTMAP:
                self.key(f"shift-{SHIFTMAP[ch]}")
            elif ch in KEYMAP:
                self.key(KEYMAP[ch])
            elif ch.isalnum():
                self.key(ch)
            else:
                print(f"warn: cannot type {ch!r}", file=sys.stderr)

    def shot(self, path):
        ppm = os.path.join(self.tmpdir, "shot.ppm")
        self.cmd(f"screendump {ppm}")
        time.sleep(0.4)
        if path.endswith(".png"):
            subprocess.run(["sips", "-s", "format", "png", ppm, "--out", path],
                           check=True, stdout=subprocess.DEVNULL)
        else:
            subprocess.run(["cp", ppm, path], check=True)

    def serial(self):
        try:
            with open(self.serial_log) as f:
                return f.read()
        except FileNotFoundError:
            return ""

    def quit(self):
        try:
            self.cmd("quit")
        except Exception:
            pass
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def run_script(q, script):
    for stmt in script.split(";"):
        stmt = stmt.strip()
        if not stmt:
            continue
        op, _, arg = stmt.partition(" ")
        if op == "type":
            q.type(arg)
        elif op == "key":
            q.key(arg)
        elif op == "wait":
            time.sleep(float(arg))
        elif op == "shot":
            q.shot(arg)
        elif op == "serial":
            print(q.serial())
        else:
            print(f"unknown op {op}", file=sys.stderr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=ISO)
    ap.add_argument("--mem", default="2G")
    ap.add_argument("--boot-wait", type=float, default=10)
    ap.add_argument("--type")
    ap.add_argument("--key")
    ap.add_argument("--wait", type=float, default=0)
    ap.add_argument("--shot")
    ap.add_argument("--script", help="semicolon-separated: type X; key ret; wait N; shot F; serial")
    ap.add_argument("--serial-out", action="store_true", help="print serial log at end")
    args = ap.parse_args()

    q = Qemu(iso=args.iso, mem=args.mem)
    try:
        time.sleep(args.boot_wait)
        if args.script:
            run_script(q, args.script)
        else:
            if args.type:
                q.type(args.type)
            if args.key:
                q.key(args.key)
            if args.wait:
                time.sleep(args.wait)
            if args.shot:
                q.shot(args.shot)
        if args.serial_out:
            print(q.serial())
    finally:
        q.quit()


if __name__ == "__main__":
    main()
