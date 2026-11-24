#pragma once
// The focus guard (专注守护) without Windows: which window may be in front while a whitelist
// category is timed, the reminder rhythm (grace, repeat after a dismiss; in strict mode the cover
// that follows an unanswered reminder), temporary releases (临时放行: each one counted, with the time
// actually spent on the released window), and the lines of the guard log. Driven by explicit
// timestamps like the pomodoro engine, so it is unit tested.

#include <algorithm>
#include <string>
#include <vector>
#include "tracker_model.h"

namespace tracker {

// ---- text helpers

inline wchar_t LowerAscii(wchar_t c) { return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + 32) : c; }
inline std::wstring LowerAscii(std::wstring text) {
    for (auto& c : text) c = LowerAscii(c);
    return text;
}
inline bool IsSpaceChar(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x3000 || c == 0x00A0;
}
inline std::wstring TrimSpace(const std::wstring& text) {
    std::size_t a = 0, b = text.size();
    while (a < b && IsSpaceChar(text[a])) ++a;
    while (b > a && IsSpaceChar(text[b - 1])) --b;
    return text.substr(a, b - a);
}
// "C:\Program Files\Microsoft\Edge\msedge.exe" -> "msedge.exe"
inline std::wstring NormalizeExe(const std::wstring& pathOrName) {
    const auto slash = pathOrName.find_last_of(L"\\/");
    return LowerAscii(TrimSpace(slash == std::wstring::npos ? pathOrName : pathOrName.substr(slash + 1)));
}
// Substring match that ignores ASCII case ("YouTube" has "youtube"; Chinese has no case).
inline bool TitleHas(const std::wstring& title, const std::wstring& keyword) {
    return !keyword.empty() && LowerAscii(title).find(LowerAscii(keyword)) != std::wstring::npos;
}
// Keywords typed by hand: spaces, commas, 顿号, semicolons, bars and line breaks separate them.
inline std::vector<std::wstring> SplitKeywords(const std::wstring& text, std::size_t limit = kMaxGuardKeywords) {
    static const std::wstring separators = L",\x3001\xff0c;\xff1b|\xff5c";
    std::vector<std::wstring> words;
    std::wstring word;
    auto flush = [&] {
        const std::wstring trimmed = TrimSpace(word);
        word.clear();
        if (trimmed.empty() || words.size() >= limit) return;
        const auto folded = LowerAscii(trimmed);
        for (const auto& existing : words) if (LowerAscii(existing) == folded) return;
        words.push_back(trimmed);
    };
    for (wchar_t c : text) {
        if (IsSpaceChar(c) || separators.find(c) != std::wstring::npos) flush();
        else word.push_back(c);
    }
    flush();
    return words;
}

// Windows that are part of the shell, not something the user chose to look at: the desktop,
// taskbar, Start, search, Alt+Tab, the lock screen, UAC, the input method. They neither count as
// a distraction nor as being back at work.
inline bool IsShellWindow(const std::wstring& exe, const std::wstring& windowClass) {
    static const wchar_t* const programs[] = {
        L"startmenuexperiencehost.exe", L"searchhost.exe", L"searchapp.exe", L"searchui.exe",
        L"shellexperiencehost.exe", L"shellhost.exe", L"lockapp.exe", L"logonui.exe", L"consent.exe",
        L"textinputhost.exe", L"ctfmon.exe", L"screenclippinghost.exe", L"dwm.exe"};
    static const wchar_t* const explorerClasses[] = {
        L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"NotifyIconOverflowWindow",
        L"TopLevelWindowForOverflowXamlIsland", L"XamlExplorerHostIslandWindow", L"MultitaskingViewFrame",
        L"TaskListThumbnailWnd", L"ForegroundStaging"};
    if (windowClass == L"#32768" || windowClass == L"tooltips_class32") return true;
    for (const auto* program : programs) if (exe == program) return true;
    if (exe == L"explorer.exe")
        for (const auto* name : explorerClasses) if (windowClass == name) return true;
    return false;
}

// ---- judging one window

enum class GuardVerdict { Neutral, Allowed, Violation };
enum class GuardReason { None, NotListed, Keyword };

// A temporary release (临时放行) of one program, or of its windows whose title has `keyword`.
struct GuardRelease {
    std::wstring exe, keyword, title;   // title: the window it was granted for
    long long start = 0, until = 0;
    long long dwell = 0;                // seconds the released window was actually in front
    int category = 0, tag = 0;
};

struct GuardResult {
    GuardVerdict verdict = GuardVerdict::Neutral;
    GuardReason reason = GuardReason::None;
    std::wstring keyword;               // Violation by Keyword: the distraction keyword in the title
    int rule = -1;                      // Allowed by a rule: its index
    int release = -1;                   // Allowed by a temporary release: its index
};

inline bool RuleApplies(const GuardRule& rule, int category) {
    return rule.scope.empty() || std::find(rule.scope.begin(), rule.scope.end(), category) != rule.scope.end();
}
inline bool ExeMatches(const std::wstring& ruleExe, const std::wstring& exe) {
    return ruleExe == L"*" || (!exe.empty() && ruleExe == exe);
}

// Whether a window may be in front while `category` (a whitelist category) is timed. A release
// that has not run out allows it first; then any applicable rule that lets it through. Callers
// decide Neutral (own windows, shell windows) before asking.
inline GuardResult Classify(const GuardConfig& config, int category, const std::wstring& exe,
                            const std::wstring& title, const std::vector<GuardRelease>& releases, long long now) {
    GuardResult result;
    for (std::size_t i = 0; i < releases.size(); ++i) {
        const auto& release = releases[i];
        if (now < release.until && ExeMatches(release.exe, exe) &&
            (release.keyword.empty() || TitleHas(title, release.keyword))) {
            result.verdict = GuardVerdict::Allowed;
            result.release = static_cast<int>(i);
            return result;
        }
    }
    std::wstring hit;
    for (std::size_t i = 0; i < config.rules.size(); ++i) {
        const auto& rule = config.rules[i];
        if (!RuleApplies(rule, category) || !ExeMatches(rule.exe, exe)) continue;
        bool allowed = false;
        switch (rule.mode) {
        case GuardMode::AllowAll:
            allowed = true;
            break;
        case GuardMode::CheckTitle: {
            std::wstring found;
            for (const auto& keyword : config.keywords) if (TitleHas(title, keyword)) { found = keyword; break; }
            allowed = found.empty();
            if (!allowed && hit.empty()) hit = found;
            break;
        }
        case GuardMode::AllowOnly:
            for (const auto& keyword : rule.keywords) allowed = allowed || TitleHas(title, keyword);
            break;
        }
        if (allowed) {
            result.verdict = GuardVerdict::Allowed;
            result.rule = static_cast<int>(i);
            return result;
        }
    }
    result.verdict = GuardVerdict::Violation;
    result.reason = hit.empty() ? GuardReason::NotListed : GuardReason::Keyword;
    result.keyword = hit;
    return result;
}

// ---- the guard log: one line per distraction ('D') and per temporary release ('R')

struct GuardRecord {
    wchar_t type = L'D';
    long long start = 0, end = 0;
    long long seconds = 0;              // D: end - start; R: time spent on the released window
    int category = 0, tag = 0;
    std::wstring exe, keyword, title;
    std::wstring action;                // D: returned released allowed switched stopped away
                                        // R: expired ended stopped
    bool operator==(const GuardRecord& o) const {
        return type == o.type && start == o.start && end == o.end && seconds == o.seconds &&
               category == o.category && tag == o.tag && exe == o.exe && keyword == o.keyword &&
               title == o.title && action == o.action;
    }
};

inline std::string GuardUtf8(const std::wstring& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t cp = static_cast<std::uint32_t>(text[i]);
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size()) {
            const auto low = static_cast<std::uint32_t>(text[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00); ++i; }
        }
        if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}
// Lenient: malformed bytes become U+FFFD.
inline std::wstring GuardWide(const std::string& bytes) {
    std::wstring out;
    for (std::size_t i = 0; i < bytes.size();) {
        const auto first = static_cast<unsigned char>(bytes[i++]);
        std::uint32_t cp = 0xFFFD;
        unsigned length = 0;
        if (first < 0x80) cp = first;
        else if (first >= 0xC2 && first <= 0xDF) { cp = first & 0x1F; length = 1; }
        else if (first >= 0xE0 && first <= 0xEF) { cp = first & 0x0F; length = 2; }
        else if (first >= 0xF0 && first <= 0xF4) { cp = first & 0x07; length = 3; }
        for (unsigned k = 0; k < length; ++k) {
            if (i >= bytes.size() || (static_cast<unsigned char>(bytes[i]) & 0xC0) != 0x80) { cp = 0xFFFD; break; }
            cp = (cp << 6) | (static_cast<unsigned char>(bytes[i++]) & 0x3F);
        }
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        if (sizeof(wchar_t) == 2 && cp > 0xFFFF) {
            out.push_back(static_cast<wchar_t>(0xD800 + ((cp - 0x10000) >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + ((cp - 0x10000) & 0x3FF)));
        } else out.push_back(static_cast<wchar_t>(cp));
    }
    return out;
}
// Tabs and line breaks would split the line; long titles are cut.
inline std::wstring GuardField(const std::wstring& text, std::size_t limit = 200) {
    std::wstring out;
    for (wchar_t c : text) {
        if (out.size() >= limit) break;
        out.push_back(c == L'\t' || c == L'\r' || c == L'\n' || c < 0x20 ? L' ' : c);
    }
    return out;
}
inline const char* GuardLogHeader() { return "type\tstart\tend\tseconds\tcategory\ttag\texe\tkeyword\ttitle\taction"; }

inline std::string FormatGuardRecord(const GuardRecord& r) {
    std::string line;
    line += r.type == L'R' ? 'R' : 'D';
    for (long long number : {r.start, r.end, r.seconds, static_cast<long long>(r.category), static_cast<long long>(r.tag)})
        line += '\t' + std::to_string(number);
    for (const auto* text : {&r.exe, &r.keyword, &r.title, &r.action}) line += '\t' + GuardUtf8(GuardField(*text));
    return line;
}

inline bool ParseGuardRecord(const std::string& raw, GuardRecord& r) {
    std::string line = raw;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    std::vector<std::string> fields;
    std::size_t from = 0;
    for (;;) {
        const auto tab = line.find('\t', from);
        fields.push_back(line.substr(from, tab == std::string::npos ? std::string::npos : tab - from));
        if (tab == std::string::npos) break;
        from = tab + 1;
    }
    if (fields.size() != 10 || (fields[0] != "D" && fields[0] != "R")) return false;
    long long numbers[5]{};
    for (int i = 0; i < 5; ++i) {
        const auto& field = fields[static_cast<std::size_t>(i + 1)];
        if (field.empty() || field.size() > 18) return false;
        std::size_t k = field[0] == '-' ? 1 : 0;
        if (k == field.size()) return false;
        long long value = 0;
        for (; k < field.size(); ++k) {
            if (field[k] < '0' || field[k] > '9') return false;
            value = value * 10 + (field[k] - '0');
        }
        numbers[i] = field[0] == '-' ? -value : value;
    }
    if (numbers[0] < 0 || numbers[1] < numbers[0] || numbers[2] < 0) return false;
    r.type = fields[0] == "R" ? L'R' : L'D';
    r.start = numbers[0];
    r.end = numbers[1];
    r.seconds = numbers[2];
    r.category = static_cast<int>(numbers[3]);
    r.tag = static_cast<int>(numbers[4]);
    r.exe = GuardWide(fields[6]);
    r.keyword = GuardWide(fields[7]);
    r.title = GuardWide(fields[8]);
    r.action = GuardWide(fields[9]);
    return true;
}

// ---- the reminder rhythm

enum class GuardState { Off = 0, Watching = 1, Pending = 2, Alerting = 3 };

struct GuardInput {
    bool applies = false;               // the guard is on, a whitelist category is timed, not a break
    bool idle = false;                  // nobody at the keyboard
    long long lastInput = 0;            // when idle: the moment of the last input
    bool neutral = false;               // DayLedger itself, the shell, no window in front
    std::wstring exe, title;            // the window in front
    int category = 0, tag = 0;          // what is being timed
};

// 强制模式 (strict): the shield's escape buttons wait this long; loosening the guard while focused
// waits for the cooldown.
constexpr long long kGuardEscapeWait = 10, kGuardCooldown = 60;

struct GuardOutput {
    bool show = false, hide = false, refresh = false, sound = false;
    bool cover = false;                 // 强制模式: the distracting window was just covered
    GuardResult result;                 // how the window in front was judged
    std::vector<GuardRecord> records;   // finished distractions and releases, for the log
};

class GuardMonitor {
public:
    static constexpr long long kMaxStep = 10;   // longer gaps between updates (sleep) are not dwell time

    GuardState state = GuardState::Off;
    long long since = 0;                // when the current violation began
    long long alertedAt = 0, dismissedAt = 0, lastUpdate = 0;
    bool popupVisible = false;
    bool shielded = false;              // 强制模式: distracting windows are covered until the episode ends
    long long shieldedAt = 0;
    bool snap = false;                  // a release or free time just ran out: the next violation is
                                        // reminded of (and in strict mode covered) without grace
    int category = 0, tag = 0;          // what was timed when the violation began
    std::wstring exe, title, keyword;   // the latest violating window
    GuardReason reason = GuardReason::None;
    std::vector<GuardRelease> releases;
    int lastRelease = -1;               // the release that allowed the window seen at lastUpdate

    // One look at the window in front.
    GuardOutput update(const GuardConfig& config, const GuardInput& in, long long now) {
        GuardOutput out;
        accrue(now);
        expire(now, out);
        lastRelease = -1;
        if (!in.applies) {
            stopAll(now, L"stopped", out);
            state = GuardState::Off;
            return out;
        }
        if (state == GuardState::Off) state = GuardState::Watching;
        if (in.idle) {
            if (state == GuardState::Alerting) endEpisode((std::max)(since, (std::min)(in.lastInput, now)), L"away", out);
            reset();
            return out;
        }
        if (in.neutral) return out;     // DayLedger itself, the shell: nothing changes
        out.result = Classify(config, in.category, in.exe, in.title, releases, now);
        if (out.result.verdict == GuardVerdict::Allowed) {
            if (state == GuardState::Alerting) endEpisode(now, L"returned", out);
            reset();
            snap = false;
            lastRelease = out.result.release;
            return out;
        }
        if (state != GuardState::Watching && in.tag != tag) {       // timing something else now
            if (state == GuardState::Alerting) endEpisode(now, L"stopped", out);
            reset();
        }
        exe = in.exe;
        title = in.title;
        keyword = out.result.keyword;
        reason = out.result.reason;
        if (state == GuardState::Watching) {
            state = GuardState::Pending;
            since = now;
            category = in.category;
            tag = in.tag;
        }
        if (state == GuardState::Pending) {
            if (snap || now - since >= static_cast<long long>(config.graceSeconds)) {
                state = GuardState::Alerting;
                alertedAt = now;
                dismissedAt = 0;
                out.sound = true;
                if (config.strict() && (snap || config.shieldSeconds == 0)) coverNow(now, out);
                else popupVisible = out.show = true;
                snap = false;
            }
            return out;
        }
        if (config.strict()) {          // an unanswered reminder turns into the shield
            if (!shielded && now - alertedAt >= static_cast<long long>(config.shieldSeconds)) coverNow(now, out);
            else out.refresh = true;
            return out;
        }
        if (shielded) {                 // strict mode was switched off: back to reminders
            shielded = false;
            shieldedAt = 0;
            dismissedAt = 0;
        }
        if (!popupVisible && now - dismissedAt >= static_cast<long long>(config.repeatSeconds)) {
            popupVisible = true;
            out.show = out.sound = true;
        } else if (popupVisible) out.refresh = true;
        return out;
    }

    // The reminder was closed; it comes back after the repeat interval if nothing changes.
    GuardOutput dismiss(long long now) {
        GuardOutput out;
        if (popupVisible) out.hide = true;
        popupVisible = false;
        dismissedAt = now;
        return out;
    }

    // 强制模式: the reminder was closed unanswered, so the window is covered now.
    GuardOutput cover(long long now) {
        GuardOutput out;
        if (state == GuardState::Alerting) coverNow(now, out);
        return out;
    }

    // Free time that was promised to end just did: the next violation gets no grace.
    void armSnap() { snap = true; }

    // A user action settled the distraction ("allowed", "switched", ...).
    GuardOutput finish(long long now, const std::wstring& action) {
        GuardOutput out;
        accrue(now);
        if (state == GuardState::Alerting) endEpisode(now, action, out);
        reset();
        return out;
    }

    // 临时放行: the violating window (its program, or the program's windows with the keyword) is
    // allowed for `seconds`; the time spent on it from now on is counted.
    GuardOutput release(long long now, long long seconds) {
        GuardOutput out;
        if (state != GuardState::Alerting && state != GuardState::Pending) return out;
        accrue(now);
        GuardRelease granted;
        granted.exe = exe;
        granted.keyword = reason == GuardReason::Keyword ? keyword : std::wstring();
        granted.title = title;
        granted.start = now;
        granted.until = now + (std::max)(1LL, seconds);
        granted.category = category;
        granted.tag = tag;
        if (state == GuardState::Alerting) endEpisode(now, L"released", out);
        reset();
        releases.push_back(granted);
        lastRelease = static_cast<int>(releases.size()) - 1;   // it is in front right now
        lastUpdate = now;
        return out;
    }

    // Ends a release early ("ended" by the user).
    GuardOutput endRelease(std::size_t index, long long now, const std::wstring& action = L"ended") {
        GuardOutput out;
        if (index >= releases.size()) return out;
        accrue(now);
        out.records.push_back(releaseRecord(releases[index], now, action));
        releases.erase(releases.begin() + static_cast<std::ptrdiff_t>(index));
        if (lastRelease == static_cast<int>(index)) lastRelease = -1;
        else if (lastRelease > static_cast<int>(index)) --lastRelease;
        return out;
    }

    // The guard stops looking (timer stopped, free time, guard off, sleep, exit).
    GuardOutput stop(long long now, const std::wstring& action) {
        GuardOutput out;
        accrue(now);
        stopAll(now, action, out);
        state = GuardState::Off;
        return out;
    }

    // The running release a window is in, for display: remaining seconds (0 = none).
    long long releaseLeft(std::size_t index, long long now) const {
        return index < releases.size() ? (std::max)(0LL, releases[index].until - now) : 0;
    }

private:
    void accrue(long long now) {
        if (lastUpdate > 0 && now > lastUpdate && lastRelease >= 0 && lastRelease < static_cast<int>(releases.size())) {
            auto& release = releases[static_cast<std::size_t>(lastRelease)];
            const long long to = (std::min)(now, release.until);
            if (to > lastUpdate) release.dwell += (std::min)(to - lastUpdate, kMaxStep);
        }
        lastUpdate = now;
    }
    void expire(long long now, GuardOutput& out) {
        for (std::size_t i = 0; i < releases.size();) {
            if (now >= releases[i].until) {
                if (lastRelease == static_cast<int>(i)) snap = true;   // its page is still in front
                out.records.push_back(releaseRecord(releases[i], releases[i].until, L"expired"));
                releases.erase(releases.begin() + static_cast<std::ptrdiff_t>(i));
                if (lastRelease == static_cast<int>(i)) lastRelease = -1;
                else if (lastRelease > static_cast<int>(i)) --lastRelease;
            } else ++i;
        }
    }
    void stopAll(long long now, const std::wstring& action, GuardOutput& out) {
        if (state == GuardState::Alerting) endEpisode(now, action, out);
        for (const auto& release : releases) out.records.push_back(releaseRecord(release, now, action));
        releases.clear();
        lastRelease = -1;
        snap = false;
        reset();
    }
    void coverNow(long long now, GuardOutput& out) {
        if (!shielded) {
            shielded = true;
            shieldedAt = now;
            out.cover = true;
        }
        if (popupVisible) out.hide = true;
        popupVisible = false;
    }
    void endEpisode(long long end, const std::wstring& action, GuardOutput& out) {
        GuardRecord record;
        record.type = L'D';
        record.start = since;
        record.end = (std::max)(end, since);
        record.seconds = record.end - record.start;
        record.category = category;
        record.tag = tag;
        record.exe = exe;
        record.keyword = keyword;
        record.title = title;
        record.action = action;
        out.records.push_back(record);
        if (popupVisible) out.hide = true;
        popupVisible = false;
    }
    static GuardRecord releaseRecord(const GuardRelease& release, long long end, const std::wstring& action) {
        GuardRecord record;
        record.type = L'R';
        record.start = release.start;
        record.end = (std::max)(release.start, (std::min)(end, release.until));
        record.seconds = release.dwell;
        record.category = release.category;
        record.tag = release.tag;
        record.exe = release.exe;
        record.keyword = release.keyword;
        record.title = release.title;
        record.action = action;
        return record;
    }
    void reset() {
        if (state != GuardState::Off) state = GuardState::Watching;
        since = alertedAt = dismissedAt = shieldedAt = 0;
        popupVisible = shielded = false;
        exe.clear();
        title.clear();
        keyword.clear();
        reason = GuardReason::None;
    }
};

} // namespace tracker
