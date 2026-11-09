#!/usr/bin/env python3
"""E2E terminal test: real PTY, Korean paste, save, abrupt kill and journal replay."""
import fcntl
import os
import pty
import signal
import sqlite3
import struct
import subprocess
import tempfile
import termios
import time
from pathlib import Path

BIN = Path("target/debug/repo").resolve()

def start(project: Path):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 34, 120, 0, 0))
    env = {**os.environ, "TERM": "xterm-256color"}
    proc = subprocess.Popen(
        [str(BIN), str(project)], stdin=slave, stdout=slave, stderr=slave,
        env=env, start_new_session=True, close_fds=True
    )
    os.close(slave)
    time.sleep(0.55)
    if proc.poll() is not None:
        raise RuntimeError("Rust TUI process exited without accepting terminal input")
    return proc, master

def paste(master: int, text: str):
    os.write(master, b"\x1b[200~" + text.encode("utf-8") + b"\x1b[201~")
    time.sleep(0.55)

def stop(proc, master):
    os.write(master, b"\x11")  # Ctrl+Q
    try:
        result = proc.wait(timeout=8)
    finally:
        os.close(master)
    assert result == 0, f"TUI returned {result}"

with tempfile.TemporaryDirectory(prefix="repo-tui-pty-") as tmp:
    project = Path(tmp) / "my-story"
    project.mkdir()
    proc, fd = start(project)
    source = "# 병원 복도 - 밤\n[[김철수]]\n@@ 영희는 어디 있어? @@\n"
    paste(fd, source)
    os.write(fd, b"\x13")  # Ctrl+S
    time.sleep(0.35)
    stop(proc, fd)

    manuscript = project / "script/main.txt"
    assert manuscript.read_text(encoding="utf-8") == source
    db_path = project / "project.sqlite"
    with sqlite3.connect(db_path) as db:
        count = db.execute("SELECT COUNT(*) FROM repo_edit_journal").fetchone()[0]
        assert count >= 1, "SQLite journal missing actual text edits"
        snapshots = db.execute("SELECT COUNT(*) FROM repo_snapshots").fetchone()[0]
        assert snapshots >= 2, "No journal snapshots on Ctrl+S"

    proc, fd = start(project)
    new_text = "## 삭제하지 말아야 할 문장입니다. ##"
    paste(fd, new_text)
    # Kill before idle autosave (3 s) and verify journal has the new edit.
    os.killpg(proc.pid, signal.SIGKILL)
    proc.wait(timeout=8)
    os.close(fd)
    assert new_text not in manuscript.read_text(encoding="utf-8"), "Autosave ran too early"

    proc, fd = start(project)
    os.write(fd, b"\x13")  # Ctrl+S recovered buffer
    time.sleep(0.4)
    stop(proc, fd)
    restored = manuscript.read_text(encoding="utf-8")
    assert new_text in restored, "Journal replay did not restore crashed session"

print("PASS: PTY live input, Korean text, Ctrl+S, WAL persistence, hard-kill recovery")
