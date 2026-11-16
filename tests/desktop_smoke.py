#!/usr/bin/env python3
"""Control-ID desktop smoke. Windows Python and interactive desktop required; no installs."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
COUNT, BM_CLICK, WM_CLOSE = 0x1004, 0xF5, 0x10
NAME, RENAMED, COLOR = "桌面冒烟中文标签", "中文标签已重命名", 0xE06831


def need(value, message):
    if not value:
        raise AssertionError(message)
    return value


class Desktop:
    def __init__(self):
        self.u = C.WinDLL("user32", use_last_error=True)
        self.callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        signatures = {
            "EnumWindows": ([self.callback, W.LPARAM], W.BOOL),
            "EnumChildWindows": ([W.HWND, self.callback, W.LPARAM], W.BOOL),
            "GetWindowThreadProcessId": ([W.HWND, C.POINTER(W.DWORD)], W.DWORD),
            "GetClassNameW": ([W.HWND, W.LPWSTR, C.c_int], C.c_int),
            "GetDlgItem": ([W.HWND, C.c_int], W.HWND),
            "GetWindow": ([W.HWND, W.UINT], W.HWND),
            "IsWindow": ([W.HWND], W.BOOL),
            "IsWindowVisible": ([W.HWND], W.BOOL),
            "IsWindowEnabled": ([W.HWND], W.BOOL),
            "GetWindowRect": ([W.HWND, C.POINTER(W.RECT)], W.BOOL),
            "GetClientRect": ([W.HWND, C.POINTER(W.RECT)], W.BOOL),
            "ClientToScreen": ([W.HWND, C.POINTER(W.POINT)], W.BOOL),
            "GetDpiForWindow": ([W.HWND], W.UINT),
            "MoveWindow": ([W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.BOOL], W.BOOL),
            "SetForegroundWindow": ([W.HWND], W.BOOL),
            "SendMessageTimeoutW": ([W.HWND, W.UINT, W.WPARAM, W.LPARAM, W.UINT,
                                     W.UINT, C.POINTER(C.c_size_t)], C.c_ssize_t),
            "PostMessageW": ([W.HWND, W.UINT, W.WPARAM, W.LPARAM], W.BOOL),
        }
        for name, (args, result) in signatures.items():
            f = getattr(self.u, name)
            f.argtypes, f.restype = args, result
        self.u.SetProcessDpiAwarenessContext(C.c_void_p(-4))
        self.buffers = []  # Keep marshaled WM_SETTEXT buffers alive, even after timeout.

    def send(self, hwnd, msg, wp=0, lp=0):
        result = C.c_size_t()
        need(self.u.SendMessageTimeoutW(hwnd, msg, wp, lp, 2, 2000, C.byref(result)),
             f"Message {msg:#x} failed/timed out")
        return C.c_ssize_t(result.value).value

    def post(self, hwnd, msg, wp=0, lp=0):
        need(self.u.PostMessageW(hwnd, msg, wp, lp), f"PostMessage {msg:#x} failed")

    def click(self, hwnd):
        need(self.u.IsWindowEnabled(hwnd), "Click target disabled")
        self.post(hwnd, BM_CLICK)  # Never synchronously enter a modal loop.

    def control(self, hwnd, identifier):
        return need(self.u.GetDlgItem(hwnd, identifier), f"Missing control {identifier}")

    def text(self, hwnd):
        b = C.create_unicode_buffer(4096)
        self.send(hwnd, 0xD, len(b), C.addressof(b))
        return b.value

    def set_text(self, hwnd, value):
        b = C.create_unicode_buffer(value)
        self.buffers.append(b)
        need(self.send(hwnd, 0xC, 0, C.addressof(b)), "WM_SETTEXT failed")
        need(self.text(hwnd) == value, "Unicode text did not round-trip")

    def cls(self, hwnd):
        b = C.create_unicode_buffer(256)
        self.u.GetClassNameW(hwnd, b, len(b))
        return b.value

    def windows(self, pid):
        found = []

        @self.callback
        def collect(hwnd, _):
            p = W.DWORD()
            self.u.GetWindowThreadProcessId(hwnd, C.byref(p))
            if p.value == pid and self.u.IsWindowVisible(hwnd):
                found.append(int(hwnd))
            return True

        self.u.EnumWindows(collect, 0)
        return found

    def top(self, pid, cls, owner=None):
        return next((h for h in self.windows(pid) if self.cls(h) == cls
                     and (owner is None or self.u.GetWindow(h, 4) == owner)), None)

    def warning_button(self, warning):
        buttons = []

        @self.callback
        def collect(hwnd, _):
            if self.cls(hwnd) == "Button" and self.u.IsWindowVisible(hwnd):
                buttons.append(hwnd)
            return True

        self.u.EnumChildWindows(warning, collect, 0)
        return buttons[0] if len(buttons) == 1 else None

    def rect(self, hwnd, client=False):
        r = W.RECT()
        need((self.u.GetClientRect if client else self.u.GetWindowRect)(hwnd, C.byref(r)),
             "Cannot read rectangle")
        return r.left, r.top, r.right, r.bottom

    def key(self, hwnd, key):
        self.post(hwnd, 0x100, key, 1)
        self.post(hwnd, 0x101, key, 0xC0000001)

    def row(self, hwnd, index):
        scale = self.u.GetDpiForWindow(hwnd) / 96
        point = round(60 * scale) | (round((24 + index * 22 + 11) * scale) << 16)
        self.post(hwnd, 0x201, 1, point)
        self.post(hwnd, 0x202, 0, point)


def wait(probe, timeout, process):
    deadline = time.monotonic() + timeout
    while True:
        need(process.poll() is None, f"Application exited early: {process.returncode}")
        value = probe()
        if value:
            return value
        if time.monotonic() >= deadline:
            raise TimeoutError("Desktop/state condition did not become true")
        time.sleep(0.1)


def parse_tracker(path):
    """Read the versioned little-endian ledger with UTF-8 names."""
    data, offset = path.read_bytes(), 8
    need(data[:8] in (b"TRACKER1", b"TRACKER2"), "Invalid tracker header")

    def number(size=4):
        nonlocal offset
        need(offset + size <= len(data), "Truncated tracker.dat")
        n = struct.unpack_from("<I" if size == 4 else "<Q", data, offset)[0]
        offset += size
        return n

    tags = []
    count = number()
    need(count <= 4096, "Invalid tag count")
    for _ in range(count):
        identifier, color, category, length = (number() for _ in range(4))
        need(0 < length <= 4096 and offset + length <= len(data), "Invalid name length")
        name = data[offset:offset + length].decode("utf-8", errors="strict")
        offset += length
        tags.append(dict(id=identifier, color=color, category=category, name=name))
    count = number()
    need(count <= 1000000, "Invalid session count")
    sessions = [dict(tag=number(), start=number(8), end=number(8)) for _ in range(count)]
    active, since = number(), number(8)
    last_saved = number(8) if data[:8] == b"TRACKER2" else 0
    need(offset == len(data), "Unexpected trailing bytes")
    ids = {t["id"] for t in tags}
    need(len(ids) == len(tags) and all(0 < i <= 0x7FFFFFFF for i in ids), "Invalid tag IDs")
    need(all(t["color"] <= 0xFFFFFF and t["category"] <= 2 and "\0" not in t["name"]
             for t in tags), "Invalid tag values")
    previous = 0
    for s in sorted(sessions, key=lambda s: s["start"]):
        need(s["tag"] in ids and previous <= s["start"] < s["end"] <= 253402300799,
             "Invalid or overlapping sessions")
        previous = s["end"]
    need((active == 0 and since == 0) or
         (active in ids and previous <= since <= 253402300799), "Invalid active session")
    return dict(tags=tags, sessions=sessions, active=active, since=since)


def exercise(d, main, p, timeout, data, check):
    c = lambda i: d.control(main, i)
    until = lambda probe: wait(probe, timeout, p)
    tags, records = c(104), c(105)
    for i in range(101, 111):
        need(d.cls(c(i)) == ("SysListView32" if i in (104, 105) else "Button"),
             f"Incorrect control class {i}")
    until(lambda: d.send(tags, COUNT) == 3)
    need(d.send(records, COUNT) == 0, "Fresh ledger has records")
    need(d.u.IsWindowEnabled(c(109)) and not d.u.IsWindowEnabled(c(110)), "Initial buttons wrong")
    check("isolated_defaults_and_native_controls")

    def editor(identifier):
        d.click(c(identifier))
        return until(lambda: d.top(p.pid, "DayLedgerTagEditor", main))

    def finish(e, identifier=1):
        d.click(d.control(e, identifier))
        until(lambda: not d.u.IsWindow(e) and d.u.IsWindowEnabled(main))

    e = editor(106)
    d.set_text(d.control(e, 201), NAME)
    need(d.send(d.control(e, 202), 0x14E, 2) == 2, "Category selection failed")
    d.click(d.control(e, 203))
    picker = until(lambda: d.top(p.pid, "#32770", e))
    for identifier in (706, 707, 708):  # Standard ChooseColor RGB edits.
        need(d.cls(d.control(picker, identifier)) == "Edit", "Missing native RGB edit")
    need(not d.u.IsWindowEnabled(e), "Picker not modal")
    d.post(picker, WM_CLOSE)
    until(lambda: not d.u.IsWindow(picker) and d.u.IsWindowEnabled(e))
    check("native_choose_color_inspected_and_canceled")
    finish(e)
    until(lambda: d.send(tags, COUNT) == 4)
    added = next(t for t in parse_tracker(data)["tags"] if t["name"] == NAME)
    need(added["category"] == 2 and added["color"] == COLOR, "Tag category/color lost")
    check("add_chinese_tag_category_color_persisted")

    e = editor(107)
    need(d.text(d.control(e, 201)) == NAME, "Wrong selected tag after add")
    d.set_text(d.control(e, 201), RENAMED)
    finish(e)
    renamed = next(t for t in parse_tracker(data)["tags"] if t["id"] == added["id"])
    need(renamed == dict(added, name=RENAMED), "Rename lost values")
    check("edit_rename_persisted")

    before = parse_tracker(data)
    e = editor(107)
    d.set_text(d.control(e, 201), "")
    d.click(d.control(e, 1))
    warning = until(lambda: d.top(p.pid, "#32770", e))
    need(d.text(warning) == "检查标签名称" and not d.u.IsWindowEnabled(e), "Wrong warning")
    warning_ok = until(lambda: d.warning_button(warning))
    d.click(warning_ok)
    until(lambda: not d.u.IsWindow(warning) and d.u.IsWindowEnabled(e))
    need(d.u.IsWindow(e), "Invalid name closed editor")
    finish(e, 2)
    need(parse_tracker(data) == before, "Canceled edit changed ledger")
    check("empty_name_modal_warning_dismiss_and_cancel")

    def start(expected):
        d.click(c(109))
        until(lambda: d.u.IsWindowEnabled(c(110)) and "暂停" in d.text(c(109)))
        need(not d.u.IsWindowEnabled(c(108)), "Active tag deletion allowed")
        need(parse_tracker(data)["active"] == expected, "Wrong active tag saved")

    def pause():
        d.click(c(109))
        until(lambda: not d.u.IsWindowEnabled(c(110)))
        need(parse_tracker(data)["active"] == 0, "Pause not saved")

    start(added["id"])
    time.sleep(1.3)  # Required elapsed whole second; not a painted-clock assertion.
    pause()
    until(lambda: d.send(records, COUNT) > 0)
    need(any(s["tag"] == added["id"] and s["end"] - s["start"] >= 1
             for s in parse_tracker(data)["sessions"]), "No positive-duration session")
    check("start_elapsed_second_pause_record_count")

    # Pointer-free selection: mouse client coordinates and native list keyboard.
    d.row(tags, 0)
    d.key(tags, 0x24)  # VK_HOME
    until(lambda: d.u.IsWindowEnabled(c(109)))
    # WM_NULL is a synchronous queue barrier after the posted mouse/key pair.
    d.send(tags, 0)
    start(1)
    time.sleep(1.3)
    d.key(tags, 0x24)
    d.key(tags, 0x28)  # VK_DOWN
    until(lambda: d.u.IsWindowEnabled(c(109)))
    start(2)
    need(any(s["tag"] == 1 for s in parse_tracker(data)["sessions"]), "Switch not saved")
    time.sleep(1.3)
    pause()
    final = parse_tracker(data)
    need(len(final["sessions"]) >= 2 and {1, 2, added["id"]} <=
         {s["tag"] for s in final["sessions"]}, "Missing switched sessions")
    check("keyboard_switch_and_pause_binary_nonoverlap")

    rows = d.send(records, COUNT)
    d.click(c(101))
    until(lambda: d.send(records, COUNT) == 0)
    d.click(c(102))
    until(lambda: d.send(records, COUNT) == rows)
    need(not d.u.IsWindowEnabled(c(102)) and not d.u.IsWindowEnabled(c(103)), "Today navigation should be disabled")
    check("previous_next_today_navigation")

    scale = d.u.GetDpiForWindow(main) / 96
    outer, client = d.rect(main), d.rect(main, True)
    width = round(850 * scale) + outer[2] - outer[0] - client[2]
    height = round(650 * scale) + outer[3] - outer[1] - client[3]
    need(d.u.MoveWindow(main, outer[0], outer[1], width, height, True), "Resize failed")
    d.text(main)  # Queue/layout barrier, not a painted text assertion.
    client = d.rect(main, True)
    need(abs(client[2] - 850 * scale) <= 1 and abs(client[3] - 650 * scale) <= 1,
         "Minimum client dimensions wrong")
    origin = W.POINT(0, 0)
    need(d.u.ClientToScreen(main, C.byref(origin)), "Client origin unavailable")
    rectangles = []
    for i in range(101, 111):
        l, t, r, b = d.rect(c(i))
        box = l - origin.x, t - origin.y, r - origin.x, b - origin.y
        need(0 <= box[0] < box[2] <= client[2] and 0 <= box[1] < box[3] <= client[3],
             f"Clipped/empty control {i}: {box}")
        for j, other in rectangles:
            need(min(box[2], other[2]) <= max(box[0], other[0]) or
                 min(box[3], other[3]) <= max(box[1], other[1]), f"Controls {i}/{j} overlap")
        rectangles.append((i, box))
    check("minimum_size_child_bounds_no_overlap")
    return final


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "build" / "TimeTracker.exe")
    parser.add_argument("--screenshot", type=Path, default=ROOT / "build" / "time-tracker-smoke.png")
    parser.add_argument("--timeout", type=float, default=12)
    parser.add_argument("--inspect-only", action="store_true")
    args = parser.parse_args()
    if not 0 < args.timeout <= 120:
        parser.error("--timeout must be greater than zero and at most 120")
    result = dict(checks=[], errors=[], painted_text_asserted=False)
    p = d = window = None
    stage = "initialization"
    check = result["checks"].append
    try:
        need(os.name == "nt", "Requires Windows Python and interactive desktop")
        exe = args.exe.resolve()
        need(exe.is_file(), f"Executable unavailable: {exe}")
        d = Desktop()
        with tempfile.TemporaryDirectory(prefix="time-tracker-smoke-") as isolated:
            env = dict(os.environ, LOCALAPPDATA=isolated, APPDATA=isolated, TEMP=isolated, TMP=isolated)
            data = Path(isolated) / "DayLedger" / "tracker.dat"

            def launch():
                return subprocess.Popen([str(exe)], cwd=str(exe.parent), env=env,
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

            def close():
                d.post(window, WM_CLOSE)
                need(p.wait(timeout=args.timeout) == 0, "Nonzero graceful shutdown")

            try:
                stage = "desktop exercises"
                p = launch()
                window = wait(lambda: d.top(p.pid, "DayLedgerNativeWindow"), args.timeout, p)
                check("visible_main_window")
                if not args.inspect_only:
                    expected = exercise(d, window, p, args.timeout, data, check)
                stage = "screenshot"
                from PIL import ImageGrab
                output = args.screenshot.resolve()
                need(output.parent.is_dir(), "Screenshot directory missing")
                d.u.SetForegroundWindow(window)
                time.sleep(0.3)
                ImageGrab.grab(bbox=d.rect(window), all_screens=True).save(output)
                result["screenshot"] = str(output)
                check("screenshot_captured_before_close")
                stage = "graceful shutdown"
                close()
                check("graceful_shutdown")
                if not args.inspect_only:
                    saved = parse_tracker(data)
                    need(saved == expected and saved["active"] == saved["since"] == 0,
                         "Shutdown changed ledger or retained active timer")
                    check("closed_binary_chinese_category_color_sessions_active_zero")
                    stage = "reopen persisted ledger"
                    p = launch()
                    window = wait(lambda: d.top(p.pid, "DayLedgerNativeWindow"), args.timeout, p)
                    wait(lambda: d.send(d.control(window, 104), COUNT) == len(saved["tags"])
                         and d.send(d.control(window, 105), COUNT) == len(saved["sessions"]), args.timeout, p)
                    need(not d.u.IsWindowEnabled(d.control(window, 110)), "Reopened timer active")
                    need(d.u.IsWindowEnabled(d.control(window, 106)), "Reopened ledger read-only")
                    check("reopen_same_isolated_directory_tag_and_record_rows")
                    close()
                    need(parse_tracker(data) == saved, "Reopen changed persisted ledger")
                    check("reopened_graceful_shutdown_binary_unchanged")
                    result["persisted"] = dict(tags=len(saved["tags"]), sessions=len(saved["sessions"]),
                                               active=0, chinese_tag=RENAMED, category=2, color=hex(COLOR))
            finally:
                if p is not None and p.poll() is None:
                    for h in d.windows(p.pid):
                        if h != window:
                            d.post(h, WM_CLOSE)
                    if window and d.u.IsWindow(window):
                        d.post(window, WM_CLOSE)
                    try:
                        p.wait(timeout=args.timeout)
                    except subprocess.TimeoutExpired:
                        p.terminate()
                        p.wait(timeout=3)
                        result["errors"].append("Cleanup required terminating spawned process")
        result["temporary_data_removed"] = True
    except Exception as exc:
        result["errors"].append(f"{stage}: {type(exc).__name__}: {exc}")
    result["status"] = "failed" if result["errors"] else "inspect_only" if args.inspect_only else "passed"
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 1 if result["errors"] else 0


if __name__ == "__main__":
    sys.exit(main())
