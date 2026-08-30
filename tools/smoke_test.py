#!/usr/bin/env python3
"""Smoke tests for choco-os: boots the ISO and asserts on serial output."""
import sys
import time

from qemu_driver import Qemu

CHECKS = []


def check(name, ok):
    CHECKS.append((name, ok))
    print(f"{'PASS' if ok else 'FAIL'}: {name}")


def run_cmd(q, text, wait=1.0):
    q.type(text)
    q.key("ret")
    time.sleep(wait)


def wait_serial(q, needle, timeout, poll=2.0):
    """Poll the serial log until needle appears or timeout elapses."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if needle in q.serial():
            return True
        time.sleep(poll)
    return False


def main():
    q = Qemu()
    try:
        # Limine copies the ~635 MB qwen3 module before the kernel starts,
        # so boot time depends on disk speed; poll instead of a fixed sleep.
        wait_serial(q, "PIT at 1000 Hz", timeout=240)
        time.sleep(2)
        boot = q.serial()
        check("boots to shell", "choco" in boot)
        check("heap initialized", "heap:" in boot and "MiB" in boot)
        check("initrd loaded", "initrd: loaded" in boot)
        check("models registered", "stories15M.bin" in boot)
        check("timer at 1000 Hz", "PIT at 1000 Hz" in boot)

        run_cmd(q, "echo Shift+Symbols OK: @#$%")
        check("echo with symbols", "Shift+Symbols OK: @#$%" in q.serial())

        run_cmd(q, "ls /")
        check("ls shows models dir", "models" in q.serial())

        run_cmd(q, "cat /etc/motd")
        check("cat motd", "Welcome to Choco OS" in q.serial())

        run_cmd(q, "write /notes.txt hello ramfs")
        run_cmd(q, "cat /notes.txt")
        check("ramfs write+read", "hello ramfs" in q.serial())

        run_cmd(q, "mkdir /tmp/a/b")
        run_cmd(q, "ls /tmp/a")
        check("mkdir -p", "\nb" in q.serial() or "b" in q.serial())

        run_cmd(q, "testmalloc", wait=3)
        check("64 MiB allocation", "4. 64 MiB alloc: PASS" in q.serial())

        run_cmd(q, "ps")
        s = q.serial()
        check("scheduler threads listed", "shell" in s and "idle" in s)

        run_cmd(q, "date")
        check("rtc date", "20" in q.serial().split("date")[-1])

        run_cmd(q, "llm -m stories260K -n 80 Once upon a time", wait=15)
        s = q.serial()
        check("llm generates", "tok/s]" in s)
        check("llm output has words", "the" in s.split("tok/s]")[0][-400:])

        tok_runs = q.serial().count("tok/s]")
        run_cmd(q, "llm -m qwen3 -t 0 -n 12 Say hello", wait=1)
        check("qwen3 loads", wait_serial(q, "qwen: loaded", timeout=120))
        deadline = time.time() + 900
        while time.time() < deadline and q.serial().count("tok/s]") <= tok_runs:
            time.sleep(5)
        check("qwen3 generates", q.serial().count("tok/s]") > tok_runs)

        run_cmd(q, "uptime")
        check("uptime", "up 0:" in q.serial())

        run_cmd(q, "ifconfig", wait=6)
        check("dhcp lease", "eth0: 10.0.2.15" in q.serial())

        run_cmd(q, "ping 10.0.2.2 2", wait=6)
        check("ping gateway", "2/2 received" in q.serial())

        run_cmd(q, "nslookup example.com", wait=5)
        check("dns resolve", "example.com ->" in q.serial())
    finally:
        q.quit()

    failed = [n for n, ok in CHECKS if not ok]
    print(f"\n{len(CHECKS) - len(failed)}/{len(CHECKS)} passed")
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
