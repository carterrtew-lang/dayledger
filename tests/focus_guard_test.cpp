// Tests for the focus guard's pure logic (src/focus_guard.h): matching, the reminder rhythm,
// temporary releases and the guard log lines.
#include "../src/focus_guard.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

using namespace tracker;

const std::wstring kZhihu = L"\u77e5\u4e4e";            // 知乎
const std::wstring kFeishu = L"\u98de\u4e66";           // 飞书

void textTests() {
    check(NormalizeExe(L"C:\\Program Files\\Microsoft\\Edge\\MSEDGE.EXE") == L"msedge.exe", "path to file name");
    check(NormalizeExe(L"  WeChat.exe ") == L"wechat.exe", "trim and lowercase");
    check(NormalizeExe(L"/usr/bin/Code") == L"code", "forward slashes");
    check(NormalizeExe(L"") == L"", "empty name");
    check(TitleHas(L"Watch on YouTube - Google Chrome", L"youtube"), "ASCII case ignored");
    check(TitleHas(kZhihu + L" - \u6709\u95ee\u9898", kZhihu), "Chinese keyword");
    check(!TitleHas(L"anything", L""), "empty keyword never matches");
    const auto words = SplitKeywords(L" " + kZhihu + L"\u3001bilibili\uff0cBiliBili;  \u5fae\u535a\n\u6296\u97f3|x\uff5c y ");
    check(words.size() == 6 && words[0] == kZhihu && words[1] == L"bilibili" && words[2] == L"\u5fae\u535a" &&
          words[3] == L"\u6296\u97f3" && words[4] == L"x" && words[5] == L"y", "keywords split, trimmed, deduplicated");
    check(SplitKeywords(L"a b c d", 2).size() == 2, "keyword limit");
    check(SplitKeywords(L" ,, \u3001 ").empty(), "only separators");
    check(IsShellWindow(L"explorer.exe", L"Shell_TrayWnd") && IsShellWindow(L"explorer.exe", L"WorkerW") &&
          IsShellWindow(L"searchhost.exe", L"Windows.UI.Core.CoreWindow") && IsShellWindow(L"x.exe", L"#32768"),
          "shell windows");
    check(!IsShellWindow(L"explorer.exe", L"CabinetWClass") && !IsShellWindow(L"msedge.exe", L"Chrome_WidgetWin_1"),
          "File Explorer and apps are real windows");
}

GuardConfig TestConfig() {
    GuardConfig config;
    config.enabled = true;
    config.graceSeconds = 3;
    config.repeatSeconds = 60;
    config.rules = {
        {L"wps.exe", GuardMode::AllowAll, {}, {}},
        {L"msedge.exe", GuardMode::CheckTitle, {}, {}},
        {L"chrome.exe", GuardMode::AllowOnly, {kFeishu, L"GitHub"}, {}},
        {L"wechat.exe", GuardMode::AllowAll, {}, {5}},
    };
    config.keywords = {kZhihu, L"bilibili"};
    return config;
}

void classifyTests() {
    const auto config = TestConfig();
    const std::vector<GuardRelease> none;
    auto judge = [&](int category, const std::wstring& exe, const std::wstring& title) {
        return Classify(config, category, exe, title, none, 100);
    };
    check(judge(0, L"wps.exe", kZhihu + L"2025\u8d22\u62a5\u70b9\u8bc4.docx - WPS").verdict == GuardVerdict::Allowed,
          "an allowed program's titles are not checked");
    check(judge(0, L"msedge.exe", L"Bing - Microsoft Edge").verdict == GuardVerdict::Allowed, "a clean browser title");
    const auto zhihu = judge(0, L"msedge.exe", kZhihu + L" - \u9996\u9875 - Microsoft Edge");
    check(zhihu.verdict == GuardVerdict::Violation && zhihu.reason == GuardReason::Keyword && zhihu.keyword == kZhihu,
          "a distraction keyword in a checked title");
    check(judge(0, L"msedge.exe", L"BiliBili - Edge").keyword == L"bilibili", "keyword case ignored");
    check(judge(0, L"chrome.exe", L"GitHub - Google Chrome").verdict == GuardVerdict::Allowed, "AllowOnly match");
    const auto chrome = judge(0, L"chrome.exe", L"Weather - Google Chrome");
    check(chrome.verdict == GuardVerdict::Violation && chrome.reason == GuardReason::NotListed, "AllowOnly miss");
    check(judge(0, L"wechat.exe", L"WeChat").verdict == GuardVerdict::Violation, "a rule scoped to another category");
    check(judge(5, L"wechat.exe", L"WeChat").verdict == GuardVerdict::Allowed, "a rule scoped to this category");
    check(judge(0, L"steam.exe", L"Steam").reason == GuardReason::NotListed, "a program with no rule");
    check(judge(0, L"", L"Something").verdict == GuardVerdict::Violation, "unknown program");
    auto star = config;
    star.rules.push_back({L"*", GuardMode::AllowOnly, {kZhihu}, {}});
    check(Classify(star, 0, L"msedge.exe", kZhihu + L" - Edge", none, 100).verdict == GuardVerdict::Allowed,
          "an explicit rule beats a distraction keyword");
    check(Classify(star, 0, L"", kZhihu, none, 100).verdict == GuardVerdict::Allowed, "* matches any program");
    std::vector<GuardRelease> releases(2);
    releases[0].exe = L"steam.exe";
    releases[0].until = 100;                        // already over at 100
    releases[1].exe = L"msedge.exe";
    releases[1].keyword = kZhihu;
    releases[1].until = 200;
    const auto released = Classify(config, 0, L"msedge.exe", kZhihu + L" - Edge", releases, 100);
    check(released.verdict == GuardVerdict::Allowed && released.release == 1, "a running release");
    check(Classify(config, 0, L"steam.exe", L"Steam", releases, 100).verdict == GuardVerdict::Violation,
          "an expired release no longer allows");
    check(Classify(config, 0, L"msedge.exe", L"bilibili - Edge", releases, 150).verdict == GuardVerdict::Violation,
          "a release covers its keyword only");
}

GuardInput Window(const std::wstring& exe, const std::wstring& title, int tag = 1) {
    GuardInput in;
    in.applies = true;
    in.exe = exe;
    in.title = title;
    in.category = 0;
    in.tag = tag;
    return in;
}

void monitorTests() {
    const auto config = TestConfig();
    GuardMonitor m;
    const auto work = Window(L"wps.exe", L"report.docx");
    const auto zhihu = Window(L"msedge.exe", kZhihu + L" - Edge");
    GuardInput shell = work;
    shell.neutral = true;
    GuardInput off;
    check(m.update(config, work, 1000).records.empty() && m.state == GuardState::Watching, "watching");
    auto out = m.update(config, zhihu, 1001);
    check(m.state == GuardState::Pending && !out.show && m.since == 1001, "a violation waits for the grace time");
    check(!m.update(config, zhihu, 1003).show, "still within the grace time");
    out = m.update(config, zhihu, 1004);
    check(out.show && out.sound && m.state == GuardState::Alerting && m.popupVisible, "the reminder appears");
    check(m.update(config, zhihu, 1005).refresh, "a visible reminder refreshes");
    check(m.update(config, shell, 1006).records.empty() && m.state == GuardState::Alerting && m.popupVisible,
          "clicking the reminder (DayLedger in front) changes nothing");
    out = m.dismiss(1010);
    check(out.hide && !m.popupVisible && m.state == GuardState::Alerting, "dismissed, still distracted");
    check(!m.update(config, zhihu, 1069).show, "no repeat before the interval");
    out = m.update(config, zhihu, 1070);
    check(out.show && out.sound, "the reminder repeats");
    out = m.update(config, work, 1080);
    check(out.hide && out.records.size() == 1 && m.state == GuardState::Watching, "back at work ends it");
    const auto& d = out.records[0];
    check(d.type == L'D' && d.start == 1001 && d.end == 1080 && d.seconds == 79 && d.action == L"returned" &&
          d.exe == L"msedge.exe" && d.keyword == kZhihu && d.tag == 1, "the distraction record");

    // a short visit within the grace time leaves no record
    m.update(config, zhihu, 1100);
    out = m.update(config, work, 1102);
    check(out.records.empty() && m.state == GuardState::Watching, "a glance is not a distraction");

    // grace 0 alerts at once; away from the keyboard ends at the last input
    auto quick = config;
    quick.graceSeconds = 0;
    GuardMonitor q;
    q.update(quick, work, 2000);
    check(q.update(quick, zhihu, 2001).show, "no grace time");
    GuardInput idle = zhihu;
    idle.idle = true;
    idle.lastInput = 2030;
    out = q.update(quick, idle, 2200);
    check(out.records.size() == 1 && out.records[0].end == 2030 && out.records[0].action == L"away" && out.hide,
          "away ends at the last input");
    check(q.update(quick, idle, 2300).records.empty() && q.state == GuardState::Watching, "no reminders while away");

    // switching tags ends the distraction; stopping the timer too
    GuardMonitor t;
    t.update(quick, zhihu, 3000);
    out = t.update(quick, Window(L"msedge.exe", kZhihu, 2), 3010);
    check(out.records.size() == 1 && out.records[0].action == L"stopped" && t.state == GuardState::Alerting &&
          t.tag == 2 && t.since == 3010, "another tag starts afresh");
    out = t.update(quick, off, 3020);
    check(out.records.size() == 1 && out.records[0].action == L"stopped" && t.state == GuardState::Off,
          "not applicable any more");

    // a user action settles it
    GuardMonitor a;
    a.update(quick, zhihu, 4000);
    out = a.finish(4005, L"switched");
    check(out.records.size() == 1 && out.records[0].action == L"switched" && out.hide && a.state == GuardState::Watching,
          "a user action ends the distraction");
    check(a.finish(4006, L"switched").records.empty(), "nothing left to settle");
}

void releaseTests() {
    auto config = TestConfig();
    config.graceSeconds = 0;
    GuardMonitor m;
    const auto work = Window(L"wps.exe", L"report.docx");
    const auto zhihu = Window(L"msedge.exe", kZhihu + L" - Edge");
    const auto steam = Window(L"steam.exe", L"Steam");
    m.update(config, work, 1000);
    m.update(config, zhihu, 1010);
    auto out = m.release(1020, 600);
    check(out.records.size() == 1 && out.records[0].action == L"released" && out.records[0].seconds == 10 && out.hide,
          "releasing ends the distraction");
    check(m.releases.size() == 1 && m.releases[0].keyword == kZhihu && m.releases[0].until == 1620 &&
          m.state == GuardState::Watching, "a release of the keyword");
    out = m.update(config, zhihu, 1030);
    check(out.result.verdict == GuardVerdict::Allowed && out.result.release == 0 && !out.show, "released page allowed");
    m.update(config, zhihu, 1040);
    m.update(config, work, 1050);              // left at 1050: 30 s on the page so far
    m.update(config, work, 1100);
    check(m.releases[0].dwell == 30, "only time on the released page counts");
    m.update(config, zhihu, 1200);
    for (long long t = 1201; t <= 1260; ++t) m.update(config, zhihu, t);
    check(m.releases[0].dwell == 90, "dwell adds up over visits");
    m.update(config, zhihu, 1400);             // a long gap (sleep) counts at most kMaxStep
    check(m.releases[0].dwell == 90 + GuardMonitor::kMaxStep, "gaps are capped");
    out = m.update(config, zhihu, 1630);       // the release ran out while on the page
    check(out.records.size() == 1 && out.records[0].type == L'R' && out.records[0].action == L"expired" &&
          out.records[0].end == 1620 && out.records[0].seconds == 90 + GuardMonitor::kMaxStep + 10 &&
          out.records[0].start == 1020, "the release record");
    check(m.releases.empty() && m.state == GuardState::Alerting, "after the release the reminder is back");

    // a release of a whole program, ended early by the user
    GuardMonitor p;
    p.update(config, steam, 5000);
    p.release(5000, 300);
    check(p.releases.size() == 1 && p.releases[0].keyword.empty(), "a program release");
    p.update(config, steam, 5005);
    out = p.endRelease(0, 5010);
    check(out.records.size() == 1 && out.records[0].action == L"ended" && out.records[0].seconds == 10 &&
          p.releases.empty(), "ended early");
    check(p.endRelease(3, 5011, L"ended").records.empty(), "no such release");

    // stopping the timer ends releases
    GuardMonitor s;
    s.update(config, steam, 6000);
    s.release(6000, 300);
    s.update(config, steam, 6004);
    GuardInput off;
    out = s.update(config, off, 6010);
    check(out.records.size() == 1 && out.records[0].action == L"stopped" && out.records[0].seconds == 10 &&
          s.releases.empty() && s.state == GuardState::Off, "stopping ends releases");
    check(s.release(6011, 60).records.empty() && s.releases.empty(), "nothing to release when nothing is wrong");

    // releasing while still in the grace time
    auto slow = config;
    slow.graceSeconds = 30;
    GuardMonitor g;
    g.update(slow, steam, 7000);
    out = g.release(7001, 60);
    check(out.records.empty() && g.releases.size() == 1, "a pending violation can be released without a record");
}

void logTests() {
    GuardRecord r;
    r.type = L'R';
    r.start = 1700000000;
    r.end = 1700000600;
    r.seconds = 321;
    r.category = 0;
    r.tag = 7;
    r.exe = L"msedge.exe";
    r.keyword = kZhihu;
    r.title = kZhihu + L"\t- \u95ee\u9898\r\n" + std::wstring(300, L'x') + L"\U0001F600";
    r.action = L"expired";
    const auto line = FormatGuardRecord(r);
    check(line.find('\n') == std::string::npos && line.find('\r') == std::string::npos, "one line");
    GuardRecord back;
    check(ParseGuardRecord(line + "\r\n", back), "parse a formatted line");
    check(back.type == L'R' && back.start == r.start && back.end == r.end && back.seconds == 321 && back.tag == 7 &&
          back.exe == r.exe && back.keyword == kZhihu && back.action == L"expired", "fields roundtrip");
    check(back.title.size() == 200 && back.title.substr(0, 2) == kZhihu && back.title[2] == L' ', "title cleaned and cut");
    GuardRecord emoji = r;
    emoji.title = L"\U0001F600 ok";
    check(ParseGuardRecord(FormatGuardRecord(emoji), back) && back.title == emoji.title, "characters beyond the BMP");
    check(!ParseGuardRecord(GuardLogHeader(), back), "the header is not a record");
    check(!ParseGuardRecord("X\t1\t2\t1\t0\t1\ta\tb\tc\td", back), "unknown type");
    check(!ParseGuardRecord("D\t5\t4\t1\t0\t1\ta\tb\tc\td", back), "end before start");
    check(!ParseGuardRecord("D\t1\t2\t1\t0\t1\ta\tb\tc", back), "missing field");
    check(!ParseGuardRecord("D\t1x\t2\t1\t0\t1\ta\tb\tc\td", back), "not a number");
    check(ParseGuardRecord("D\t1\t2\t1\t-1\t1\t\t\t\treturned", back) && back.category == -1 && back.exe.empty(),
          "empty texts");
    check(GuardWide(std::string("a\xff" "b")) == L"a\xfffd" L"b", "malformed bytes are replaced");
}
} // namespace

// 强制模式: an unanswered reminder covers the window; the cover follows the distraction until it ends.
void strictTests() {
    auto config = TestConfig();
    config.options |= GuardStrict;
    config.shieldSeconds = 10;
    const auto work = Window(L"wps.exe", L"report.docx");
    const auto zhihu = Window(L"msedge.exe", kZhihu + L" - Edge");
    const auto steam = Window(L"steam.exe", L"Steam");
    GuardInput shell = work;
    shell.neutral = true;
    GuardMonitor m;
    m.update(config, work, 1000);
    m.update(config, zhihu, 1001);
    auto out = m.update(config, zhihu, 1004);
    check(out.show && m.popupVisible && !m.shielded && !out.cover, "strict mode reminds first");
    out = m.update(config, zhihu, 1013);
    check(!out.cover && !m.shielded && out.refresh, "the reminder waits for an answer");
    out = m.update(config, zhihu, 1014);
    check(out.cover && out.hide && m.shielded && m.shieldedAt == 1014 && !m.popupVisible &&
          m.state == GuardState::Alerting, "unanswered for the shield time: covered");
    out = m.update(config, steam, 1020);
    check(m.shielded && !out.cover && out.records.empty() && m.exe == L"steam.exe",
          "the shield follows to the next distraction");
    check(m.update(config, shell, 1025).records.empty() && m.shielded, "the shell in front changes nothing");
    out = m.update(config, work, 1030);
    check(!m.shielded && out.records.size() == 1 && out.records[0].action == L"returned" &&
          out.records[0].start == 1001 && out.records[0].end == 1030, "back at work uncovers it");

    // closing the reminder unanswered covers at once
    GuardMonitor d;
    d.update(config, zhihu, 2000);
    d.update(config, zhihu, 2003);
    out = d.cover(2005);
    check(out.cover && out.hide && d.shielded && d.shieldedAt == 2005, "closed reminder: covered");
    check(d.cover(2006).records.empty() && !d.cover(2006).cover, "already covered");
    GuardMonitor w;
    w.update(config, zhihu, 2100);
    check(!w.cover(2101).cover && !w.shielded, "nothing to cover within the grace time");

    // shield time 0: covered without a reminder; releasing uncovers
    auto direct = config;
    direct.shieldSeconds = 0;
    GuardMonitor z;
    z.update(direct, zhihu, 3000);
    out = z.update(direct, zhihu, 3003);
    check(out.cover && !out.show && z.shielded && !z.popupVisible, "no reminder before the shield");
    out = z.release(3010, 60);
    check(!z.shielded && z.releases.size() == 1 && out.records.size() == 1, "a release uncovers");
    // the release runs out with its page in front: covered without grace
    for (long long t = 3011; t < 3070; t += 5) z.update(direct, zhihu, t);
    out = z.update(direct, zhihu, 3071);
    check(out.cover && z.shielded && z.since == 3071 && out.records.size() == 1 && out.records[0].action == L"expired",
          "an expired release covers at once");

    // without strict mode an expired release reminds at once
    auto plain = TestConfig();
    GuardMonitor e;
    e.update(plain, zhihu, 4000);
    e.update(plain, zhihu, 4003);
    e.release(4004, 30);
    e.update(plain, zhihu, 4020);
    out = e.update(plain, zhihu, 4040);
    check(out.show && !out.cover && !e.shielded && e.state == GuardState::Alerting, "expired: reminded at once");
    // a release that ran out elsewhere does not snap
    GuardMonitor f;
    f.update(plain, zhihu, 4100);
    f.update(plain, zhihu, 4103);
    f.release(4104, 30);
    f.update(plain, work, 4110);
    f.update(plain, work, 4140);
    check(!f.snap, "left before the end: no snap");
    out = f.update(plain, zhihu, 4150);
    check(!out.show && f.state == GuardState::Pending, "back on the page later: grace as usual");

    // free time ran out: the next violation is covered without grace; working clears it
    GuardMonitor s;
    GuardInput off;
    s.update(config, off, 5000);
    s.armSnap();
    out = s.update(config, zhihu, 5001);
    check(out.cover && s.shielded && s.state == GuardState::Alerting, "after free time: covered at once");
    GuardMonitor c;
    c.armSnap();
    c.update(config, work, 5100);
    out = c.update(config, zhihu, 5101);
    check(!out.cover && !out.show && !c.snap && c.state == GuardState::Pending, "back at work first: no snap");
    GuardMonitor o;
    o.armSnap();
    o.update(config, off, 5200);
    check(!o.snap, "not timing any more: no snap");

    // strict mode switched off mid-shield: reminders again
    GuardMonitor x;
    x.update(direct, zhihu, 6000);
    x.update(direct, zhihu, 6003);
    out = x.update(plain, zhihu, 6004);
    check(!x.shielded && out.show && x.popupVisible, "strict mode off: the reminder replaces the shield");
    // idle ends a covered episode
    GuardMonitor y;
    y.update(direct, zhihu, 7000);
    y.update(direct, zhihu, 7003);
    GuardInput away = zhihu;
    away.idle = true;
    away.lastInput = 7010;
    out = y.update(direct, away, 7200);
    check(!y.shielded && out.records.size() == 1 && out.records[0].action == L"away", "away uncovers");
}

int main() {
    try {
        textTests();
        classifyTests();
        monitorTests();
        releaseTests();
        strictTests();
        logTests();
        std::cout << "PASS: " << checks << " checks\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
