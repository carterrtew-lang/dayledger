#!/usr/bin/env python3
"""Desktop smoke for the custom-drawn TimeTracker.exe. Windows Python and interactive desktop
required; no installs. The window has no native controls, so the test drives the app through its
automation interface (WM_COMMAND ids and the WM_DL_QUERY state message) and verifies every step
against the binary ledger written to an isolated LOCALAPPDATA."""
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
WM_CLOSE, WM_COMMAND = 0x10, 0x111
NAME, RENAMED = "桌面冒烟中文标签", "中文标签已重命名"
VIOLET = 0xD65062  # COLORREF (0x00BBGGRR) of preset swatch 6, #6250d6

# Automation interface of TimeTracker.exe (see the command enum in src/time_tracker.cpp).
WM_DL_QUERY = 0x8000 + 0x40
(Q_TAGS, Q_RECORDS, Q_ACTIVE, Q_READONLY, Q_IS_TODAY, Q_SAVE_FAILED, Q_DARK, Q_SELECTED, Q_LOADED,
 Q_POMODORO_MODE, Q_POMODORO_PHASE, Q_POMODORO_PAUSED, Q_POMODORO_REMAINING, Q_POMODOROS_TODAY,
 Q_MINI, Q_REST_TAG) = range(16)
CMD_PREV_DAY, CMD_NEXT_DAY, CMD_TODAY = 101, 102, 103
CMD_NEW_TAG, CMD_STOP, CMD_ADD_RECORD, CMD_UNDO = 106, 110, 111, 112
CMD_THEME_LIGHT, CMD_THEME_DARK = 121, 122
CMD_POMODORO_PAUSE, CMD_POMODORO_ABANDON, CMD_MINI, CMD_POMODORO_START = 127, 129, 130, 134
CMD_POMODORO_ON, CMD_POMODORO_OFF = 136, 137
PHASE_READY, PHASE_FOCUS, PHASE_BREAK = 0, 1, 2
# With DAYLEDGER_TEST_SECONDS_PER_MINUTE=1 these "minutes" last one second each.
POMODORO_SETTINGS = "[pomodoro]\nfocus=5\nshort=2\nlong=2\nevery=4\n"
FOCUS_SECONDS, BREAK_SECONDS = 5, 2
CMD_TAG_FIRST, CMD_EDIT_TAG_FIRST, CMD_DELETE_RECORD_FIRST = 0x1000, 0x2000, 0x4000
IDOK, IDCANCEL = 1, 2
TD_NAME, TD_CUSTOM_COLOR, TD_CATEGORY_OTHER, TD_COLOR_FIRST = 201, 203, 213, 220
RD_START, RD_END, RD_TAG_FIRST = 302, 303, 0x1000
MIN_CLIENT = (880, 600)


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

    def command(self, hwnd, identifier):
        """Posted, never sent: a command may enter a modal loop (dialogs, colour picker)."""
        self.post(hwnd, WM_COMMAND, identifier)

    def query(self, hwnd, what):
        return self.send(hwnd, WM_DL_QUERY, what)

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

    def children(self, hwnd):
        found = []

        @self.callback
        def collect(child, _):
            found.append(int(child))
            return True

        self.u.EnumChildWindows(hwnd, collect, 0)
        return found

    def top(self, pid, cls, owner=None):
        return next((h for h in self.windows(pid) if self.cls(h) == cls
                     and (owner is None or self.u.GetWindow(h, 4) == owner)), None)

    def rect(self, hwnd, client=False):
        r = W.RECT()
        need((self.u.GetClientRect if client else self.u.GetWindowRect)(hwnd, C.byref(r)),
             "Cannot read rectangle")
        return r.left, r.top, r.right, r.bottom


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
    """Read the versioned little-endian ledger with UTF-8 names (TRACKER3 adds session flags)."""
    data, offset = path.read_bytes(), 8
    need(data[:8] in (b"TRACKER1", b"TRACKER2", b"TRACKER3"), "Invalid tracker header")
    version = data[7] - ord("0")

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
    sessions = [dict(tag=number(), start=number(8), end=number(8), flags=number() if version >= 3 else 0)
                for _ in range(count)]
    active, since = number(), number(8)
    last_saved = number(8) if version >= 2 else 0
    need(offset == len(data), "Unexpected trailing bytes")
    ids = {t["id"] for t in tags}
    need(len(ids) == len(tags) and all(0 < i <= 0x7FFFFFFF for i in ids), "Invalid tag IDs")
    need(all(t["color"] <= 0xFFFFFF and t["category"] <= 2 and "\0" not in t["name"]
             for t in tags), "Invalid tag values")
    need(all(s["flags"] in (0, 1) for s in sessions), "Unknown session flags")
    previous = 0
    for s in sorted(sessions, key=lambda s: s["start"]):
        need(s["tag"] in ids and previous <= s["start"] < s["end"] <= 253402300799,
             "Invalid or overlapping sessions")
        previous = s["end"]
    need((active == 0 and since == 0) or
         (active in ids and previous <= since <= 253402300799), "Invalid active session")
    return dict(tags=tags, sessions=sessions, active=active, since=since)


def exercise(d, main, p, timeout, data, check):
    until = lambda probe: wait(probe, timeout, p)
    q = lambda what: d.query(main, what)
    until(lambda: q(Q_LOADED) == 1)
    need(q(Q_TAGS) == 3 and q(Q_RECORDS) == 0 and q(Q_ACTIVE) == 0 and q(Q_READONLY) == 0,
         "Fresh ledger state wrong")
    need(not d.children(main), "Main window should be fully custom drawn")
    check("isolated_defaults_custom_drawn_window")

    def editor(identifier, cls="DayLedgerTagEditor"):
        d.command(main, identifier)
        return until(lambda: d.top(p.pid, cls, main))

    def finish(e, identifier=IDOK):
        d.command(e, identifier)
        until(lambda: not d.u.IsWindow(e) and d.u.IsWindowEnabled(main))
        d.send(main, 0)  # WM_NULL is answered only after the command (and its save) has finished

    e = editor(CMD_NEW_TAG)
    need(not d.u.IsWindowEnabled(main), "Tag editor not modal")
    d.set_text(d.control(e, TD_NAME), NAME)
    d.command(e, TD_CATEGORY_OTHER)
    d.command(e, TD_COLOR_FIRST + 6)
    d.command(e, TD_CUSTOM_COLOR)
    picker = until(lambda: d.top(p.pid, "#32770", e))
    for identifier in (706, 707, 708):  # Standard ChooseColor RGB edits.
        need(d.cls(d.control(picker, identifier)) == "Edit", "Missing native RGB edit")
    need(not d.u.IsWindowEnabled(e), "Picker not modal")
    d.post(picker, WM_CLOSE)
    until(lambda: not d.u.IsWindow(picker) and d.u.IsWindowEnabled(e))
    check("native_choose_color_inspected_and_canceled")
    finish(e)
    until(lambda: q(Q_TAGS) == 4)
    added = next(t for t in parse_tracker(data)["tags"] if t["name"] == NAME)
    need(added["category"] == 2 and added["color"] == VIOLET, "Tag category/color lost")
    need(q(Q_SELECTED) == added["id"], "New tag not selected")
    check("add_chinese_tag_category_color_persisted")

    index = 3  # tags keep their order; the new tag is the fourth
    e = editor(CMD_EDIT_TAG_FIRST + index)
    need(d.text(d.control(e, TD_NAME)) == NAME, "Editor opened the wrong tag")
    d.set_text(d.control(e, TD_NAME), RENAMED)
    finish(e)
    renamed = next(t for t in parse_tracker(data)["tags"] if t["id"] == added["id"])
    need(renamed == dict(added, name=RENAMED), "Rename lost values")
    check("edit_rename_persisted")

    before = parse_tracker(data)
    e = editor(CMD_EDIT_TAG_FIRST + index)
    d.set_text(d.control(e, TD_NAME), "   ")
    d.command(e, IDOK)
    d.send(e, 0)  # WM_NULL: queue barrier after the posted IDOK
    time.sleep(0.3)
    need(d.u.IsWindow(e) and d.u.IsWindowEnabled(e), "Blank name closed or blocked the editor")
    need(not d.top(p.pid, "#32770", e), "Blank name should be reported inline, not in a message box")
    finish(e, IDCANCEL)
    need(parse_tracker(data) == before, "Canceled edit changed ledger")
    check("blank_name_inline_error_and_cancel")

    def start(position, expected):
        d.command(main, CMD_TAG_FIRST + position)
        until(lambda: q(Q_ACTIVE) == expected)
        need(parse_tracker(data)["active"] == expected, "Wrong active tag saved")

    def stop():
        d.command(main, CMD_STOP)
        until(lambda: q(Q_ACTIVE) == 0)
        need(parse_tracker(data)["active"] == 0, "Stop not saved")

    start(index, added["id"])
    time.sleep(1.3)  # Required elapsed whole second; not a painted-clock assertion.
    stop()
    until(lambda: q(Q_RECORDS) > 0)
    need(any(s["tag"] == added["id"] and s["end"] - s["start"] >= 1
             for s in parse_tracker(data)["sessions"]), "No positive-duration session")
    check("start_elapsed_second_stop_record_count")

    start(0, 1)
    time.sleep(1.3)
    start(1, 2)
    need(any(s["tag"] == 1 for s in parse_tracker(data)["sessions"]), "Switch not saved")
    time.sleep(1.3)
    d.command(main, CMD_TAG_FIRST + 1)  # the running tag's command stops it
    until(lambda: q(Q_ACTIVE) == 0)
    final = parse_tracker(data)
    need(final["active"] == 0 and len(final["sessions"]) >= 3 and
         {1, 2, added["id"]} <= {s["tag"] for s in final["sessions"]}, "Missing switched sessions")
    check("switch_and_toggle_stop_binary_nonoverlap")

    rows = q(Q_RECORDS)
    d.command(main, CMD_PREV_DAY)
    until(lambda: q(Q_IS_TODAY) == 0 and q(Q_RECORDS) == 0)
    e = editor(CMD_ADD_RECORD, "DayLedgerRecordEditor")
    d.command(e, RD_TAG_FIRST + 0)
    d.set_text(d.control(e, RD_START), "10:00")
    d.set_text(d.control(e, RD_END), "10:30")
    finish(e)
    until(lambda: q(Q_RECORDS) == 1)
    manual = [s for s in parse_tracker(data)["sessions"] if s["end"] - s["start"] == 1800 and s["tag"] == 1]
    need(len(manual) == 1, "Manual record not saved")
    d.command(main, CMD_DELETE_RECORD_FIRST + 0)
    until(lambda: q(Q_RECORDS) == 0)
    need(manual[0] not in parse_tracker(data)["sessions"], "Deleted record still saved")
    d.command(main, CMD_UNDO)
    until(lambda: q(Q_RECORDS) == 1)
    need(manual[0] in parse_tracker(data)["sessions"], "Undo did not restore the record")
    check("manual_record_add_delete_undo")

    d.command(main, CMD_NEXT_DAY)
    until(lambda: q(Q_IS_TODAY) == 1 and q(Q_RECORDS) == rows)
    d.command(main, CMD_NEXT_DAY)  # no future days
    d.command(main, CMD_PREV_DAY)
    d.command(main, CMD_TODAY)
    until(lambda: q(Q_IS_TODAY) == 1 and q(Q_RECORDS) == rows)
    check("previous_next_today_navigation")

    d.command(main, CMD_THEME_DARK)
    until(lambda: q(Q_DARK) == 1)
    d.command(main, CMD_THEME_LIGHT)
    until(lambda: q(Q_DARK) == 0)
    check("theme_switch")

    d.command(main, CMD_POMODORO_ON)
    until(lambda: q(Q_POMODORO_MODE) == 1 and q(Q_POMODORO_PHASE) == PHASE_READY)
    d.command(main, CMD_TAG_FIRST + 0)  # focus on the first tag
    until(lambda: q(Q_POMODORO_PHASE) == PHASE_FOCUS and q(Q_ACTIVE) == 1)
    until(lambda: q(Q_POMODORO_PHASE) == PHASE_BREAK)
    ledger, rest = parse_tracker(data), q(Q_REST_TAG)
    need(any(t["id"] == rest and t["name"] == "休息" and t["category"] == 2 for t in ledger["tags"]),
         "Rest tag not created")
    need(ledger["active"] == rest, "Break not recorded under the rest tag")
    done = [s for s in ledger["sessions"] if s["flags"] & 1]
    need(len(done) == 1 and done[0]["tag"] == 1 and done[0]["end"] - done[0]["start"] == FOCUS_SECONDS,
         "Completed focus not recorded and flagged")
    until(lambda: q(Q_POMODORO_PHASE) == PHASE_READY)  # the break ends and waits for the user
    ledger = parse_tracker(data)
    need(ledger["active"] == 0 and any(s["tag"] == rest and s["end"] - s["start"] == BREAK_SECONDS
                                       for s in ledger["sessions"]), "Break not saved")
    need(q(Q_POMODOROS_TODAY) == 1, "Pomodoro count wrong")
    check("pomodoro_focus_break_ready_cycle")

    d.command(main, CMD_POMODORO_START)
    until(lambda: q(Q_POMODORO_PHASE) == PHASE_FOCUS and q(Q_ACTIVE) == 1)
    d.command(main, CMD_POMODORO_PAUSE)
    until(lambda: q(Q_POMODORO_PAUSED) == 1 and q(Q_ACTIVE) == 0)
    held = q(Q_POMODORO_REMAINING)
    time.sleep(1.3)
    need(q(Q_POMODORO_REMAINING) == held, "Paused countdown moved")
    d.command(main, CMD_POMODORO_PAUSE)  # resume
    until(lambda: q(Q_POMODORO_PAUSED) == 0 and q(Q_ACTIVE) == 1)
    d.command(main, CMD_POMODORO_ABANDON)
    until(lambda: q(Q_POMODORO_PHASE) == PHASE_READY and q(Q_ACTIVE) == 0)
    need(q(Q_POMODOROS_TODAY) == 1 and len([s for s in parse_tracker(data)["sessions"] if s["flags"]]) == 1,
         "Abandoned pomodoro counted")
    check("pomodoro_pause_resume_abandon")

    d.command(main, CMD_MINI)
    mini = until(lambda: d.top(p.pid, "DayLedgerMini"))
    need(q(Q_MINI) == 1, "Mini mode not reported")
    d.command(main, CMD_MINI)
    until(lambda: q(Q_MINI) == 0 and not d.u.IsWindowVisible(mini) and d.u.IsWindowVisible(main))
    d.command(main, CMD_POMODORO_OFF)
    until(lambda: q(Q_POMODORO_MODE) == 0)
    check("mini_window_and_mode_switch")

    scale = d.u.GetDpiForWindow(main) / 96
    outer, client = d.rect(main), d.rect(main, True)
    frame_w, frame_h = outer[2] - outer[0] - client[2], outer[3] - outer[1] - client[3]
    need(d.u.MoveWindow(main, outer[0], outer[1], 500 + frame_w, 400 + frame_h, True), "Resize failed")
    d.text(main)  # Queue/layout barrier, not a painted text assertion.
    client = d.rect(main, True)
    need(abs(client[2] - MIN_CLIENT[0] * scale) <= 1 and abs(client[3] - MIN_CLIENT[1] * scale) <= 1,
         f"Minimum client dimensions wrong: {client}")
    check("minimum_client_size_enforced")
    final = parse_tracker(data)
    return final, q(Q_RECORDS)


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
            env = dict(os.environ, LOCALAPPDATA=isolated, APPDATA=isolated, TEMP=isolated, TMP=isolated,
                       DAYLEDGER_TEST_SECONDS_PER_MINUTE="1")
            data = Path(isolated) / "DayLedger" / "tracker.dat"
            data.parent.mkdir()
            (data.parent / "settings.ini").write_text(POMODORO_SETTINGS, encoding="ascii")

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
                    expected, rows = exercise(d, window, p, args.timeout, data, check)
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
                    wait(lambda: d.query(window, Q_LOADED) == 1 and
                         d.query(window, Q_TAGS) == len(saved["tags"]) and
                         d.query(window, Q_RECORDS) == rows, args.timeout, p)
                    need(d.query(window, Q_ACTIVE) == 0, "Reopened timer active")
                    need(d.query(window, Q_READONLY) == 0, "Reopened ledger read-only")
                    check("reopen_same_isolated_directory_tag_and_record_rows")
                    close()
                    need(parse_tracker(data) == saved, "Reopen changed persisted ledger")
                    check("reopened_graceful_shutdown_binary_unchanged")
                    result["persisted"] = dict(tags=len(saved["tags"]), sessions=len(saved["sessions"]),
                                               active=0, chinese_tag=RENAMED, category=2, color=hex(VIOLET))
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
