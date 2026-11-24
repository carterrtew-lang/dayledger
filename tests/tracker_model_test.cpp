#include "../src/tracker_model.h"
#include "../src/pomodoro.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <iterator>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

struct TempFiles {
    std::filesystem::path directory;
    TempFiles() {
        directory = std::filesystem::temp_directory_path() /
                    (L"tracker-model-test-" + std::to_wstring(std::random_device{}()));
        std::filesystem::create_directory(directory);
    }
    ~TempFiles() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

std::vector<unsigned char> readBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void writeBytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    check(static_cast<bool>(out), "write corrupt fixture");
}
std::uint64_t number(const std::vector<unsigned char>& bytes, std::size_t offset, unsigned size) {
    std::uint64_t result = 0;
    for (unsigned i = 0; i < size; ++i) result |= std::uint64_t(bytes.at(offset + i)) << (8 * i);
    return result;
}
void put(std::vector<unsigned char>& bytes, std::size_t offset, std::uint64_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i) bytes.at(offset + i) = (value >> (8 * i)) & 255;
}
// Rewrites a TRACKER6 file in an older layout: TRACKER5 has no strict mode numbers in the guard
// block, TRACKER4 no category flags and no guard block, TRACKER3 no category table either (the
// default categories are implied), TRACKER2 no session flags, TRACKER1 also no checkpoint.
std::vector<unsigned char> downgrade(const std::vector<unsigned char>& bytes, char version) {
    if (version == '5') {
        std::size_t at = 12;
        for (std::uint64_t i = 0; i < number(bytes, 8, 4); ++i) at += 16 + static_cast<std::size_t>(number(bytes, at + 12, 4));
        const std::size_t tags = at;
        at += 4;
        for (std::uint64_t i = 0; i < number(bytes, tags, 4); ++i) at += 16 + static_cast<std::size_t>(number(bytes, at + 12, 4));
        at += 4 + 24 * static_cast<std::size_t>(number(bytes, at, 4)) + 20;    // sessions, running state
        std::vector<unsigned char> out = bytes;
        out[7] = '5';
        out.erase(out.begin() + static_cast<std::ptrdiff_t>(at + 24), out.begin() + static_cast<std::ptrdiff_t>(at + 32));
        return out;
    }
    std::vector<unsigned char> out(bytes.begin(), bytes.begin() + 8);
    out[7] = static_cast<unsigned char>(version);
    std::size_t cursor = 12;
    if (version == '4') out.insert(out.end(), bytes.begin() + 8, bytes.begin() + 12);
    for (std::uint64_t i = 0; i < number(bytes, 8, 4); ++i) {
        const auto length = static_cast<std::size_t>(number(bytes, cursor + 12, 4));
        if (version == '4') {        // id, colour, name length, name: everything but the flags
            out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                       bytes.begin() + static_cast<std::ptrdiff_t>(cursor + 8));
            out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(cursor + 12),
                       bytes.begin() + static_cast<std::ptrdiff_t>(cursor + 16 + length));
        }
        cursor += 16 + length;
    }
    const std::size_t tagsStart = cursor;
    cursor += 4;
    for (std::uint64_t i = 0; i < number(bytes, tagsStart, 4); ++i)
        cursor += 16 + static_cast<std::size_t>(number(bytes, cursor + 12, 4));
    out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(tagsStart),
               bytes.begin() + static_cast<std::ptrdiff_t>(cursor + 4));
    const auto sessions = number(bytes, cursor, 4);
    cursor += 4;
    for (std::uint64_t i = 0; i < sessions; ++i, cursor += 24)
        out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                   bytes.begin() + static_cast<std::ptrdiff_t>(cursor + (version >= '3' ? 24 : 20)));
    out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
               bytes.begin() + static_cast<std::ptrdiff_t>(cursor + (version == '1' ? 12 : 20)));
    return out;            // the guard block after the checkpoint is left out
}

void timingTests() {
    tracker::Model model;
    check(model.tags.size() == 3 && model.tags[0].name == L"\u5de5\u4f5c", "Chinese defaults");
    model.start(1, 100);
    model.start(1, 120);
    check(model.activeSince == 100 && model.sessions.empty(), "same tag does not restart");
    model.start(99, 130);
    check(model.activeTagId == 1, "unknown start ignored");
    model.start(2, 160);
    check(model.sessions.size() == 1 && model.sessions[0].start == 100 &&
          model.sessions[0].end == 160 && model.activeSince == 160, "atomic switch");
    model.stop(200);
    model.stop(220);
    auto stats = model.stats(0, 300, 300);
    check(stats.ofCategory(0) == 60 && stats.ofCategory(1) == 40 && stats.ofCategory(2) == 0 && stats.total == 100,
          "switch and stop totals");
    check(stats.byTag[1] == 60 && stats.byTag[2] == 40, "per-tag totals");
    check(model.activeTagId == 0 && model.activeSince == 0 && model.sessions.size() == 2,
          "stop is idempotent");
    model.start(3, 250);
    check(model.stats(0, 400, 275).ofCategory(2) == 25, "active segment counted");
    model.stop(300);
    check(model.stats(0, 400, 400).total == 150, "pause gaps excluded");
    check(model.stats(0, 400, 180).total == 80, "closed intervals clipped to now");
    check(model.stats(400, 300, 500).total == 0, "reversed day empty");
    check(model.stats(0, 0, 500).total == 0, "empty day empty");

    tracker::Model clock;
    clock.start(1, 100);
    clock.start(2, 90);
    check(clock.activeSince == 100 && clock.sessions.empty(), "rollback switch clamps boundary");
    clock.stop(80);
    check(clock.sessions.empty() && clock.activeTagId == 0, "negative interval discarded");
    clock.start(1, 100);
    clock.stop(200);
    clock.start(2, 150);
    check(clock.activeSince == 200, "restart cannot overlap history");
    clock.start(3, 180);
    check(clock.activeSince == 200 && clock.sessions.size() == 1, "rollback switch avoids overlap");
    clock.stop(240);
    check(clock.stats(0, 300, 300).total == 140, "rollback duration not double counted");
    clock.start(1, -1);
    check(clock.activeTagId == 0, "invalid negative start ignored");
    clock.start(1, 300);
    clock.stop(-1);
    check(clock.activeTagId == 1, "invalid stop ignored");
    clock.stop(300);
    check(clock.sessions.size() == 2, "zero interval omitted");

    tracker::Model midnight;
    midnight.start(1, 86370);
    midnight.start(2, 86430);
    auto first = midnight.stats(0, 86400, 86490);
    auto second = midnight.stats(86400, 172800, 86490);
    check(first.ofCategory(0) == 30 && first.total == 30, "previous day intersection");
    check(second.ofCategory(0) == 30 && second.ofCategory(1) == 60 && second.total == 90,
          "next day and active intersection");
    midnight.stop(86520);
    check(midnight.stats(86400, 86460, 90000).total == 60, "arbitrary day boundary intersection");
}

void tagTests() {
    tracker::Model model;
    const int id = model.addTag(L"\u9605\u8bfb", 0x00563412u, 2);
    check(id == 4 && model.findTag(id)->color == 0x00563412u, "add tag preserves COLORREF");
    check(model.updateTag(id, L"Reading", 0x00112233u, 0), "update tag");
    check(model.findTag(id)->category == 0, "updated category");
    check(!model.updateTag(999, L"Missing", 0, 0), "unknown update fails");
    check(model.addTag(L"", 0, 0) == 0, "empty tag rejected");
    check(model.addTag(L"Bad", 0xFF000000u, 0) == 0, "invalid color rejected");
    check(model.addTag(L"Bad", 0, 3) == 0, "invalid category rejected");
    check(model.addTag(std::wstring(4097, L'x'), 0, 0) == 0, "oversize name rejected");
    check(model.addTag(std::wstring(1, wchar_t(0xD800)), 0, 0) == 0,
          "unpaired surrogate rejected");
    check(model.addTag(std::wstring(L"a\0b", 3), 0, 0) == 0, "embedded NUL rejected");
    model.start(id, 10);
    check(!model.removeTag(id), "active tag cannot be removed");
    model.stop(20);
    check(!model.removeTag(id), "referenced tag cannot be removed");
    check(model.updateTag(id, L"Renamed", 0, 1), "referenced tag editable");
    check(model.stats(0, 30, 30).ofCategory(1) == 10, "history uses current tag category");
    check(model.removeTag(3) && !model.findTag(3), "unused tag removable");
    check(!model.removeTag(999), "unknown remove fails");
}

void editingTests() {
    using tracker::EditStatus;
    tracker::Model model;
    check(model.lastUsedTag() == 0, "no last tag on empty ledger");
    model.start(1, 100);
    model.start(2, 200);
    model.stop(300);
    // Records: [100,200) tag 1, [200,300) tag 2.
    check(model.lastUsedTag() == 2 && model.sessionCount(1) == 1, "last used and per-tag count");

    auto result = model.addSession(3, 300, 400, 1000);
    check(result && model.sessions.size() == 3 && model.sessions[2].tagId == 3, "append manual record");
    result = model.addSession(3, 40, 90, 1000);
    check(result && model.sessions.front().start == 40, "manual record inserted in start order");
    result = model.addSession(3, 150, 250, 1000);
    check(result.status == EditStatus::Overlap && result.conflict == 1, "overlap names the conflict");
    check(model.addSession(3, 500, 600, 550).status == EditStatus::InFuture, "future record rejected");
    check(model.addSession(3, 600, 600, 1000).status == EditStatus::InvalidRange, "empty record rejected");
    check(model.addSession(99, 600, 700, 1000).status == EditStatus::UnknownTag, "unknown tag rejected");
    check(static_cast<bool>(model.addSession(3, 90, 100, 1000)), "touching records allowed");
    check(model.sessions.size() == 5, "five records");

    // Records: [40,90) 3, [90,100) 3, [100,200) 1, [200,300) 2, [300,400) 3.
    check(model.updateSession(2, 2, 100, 190, 1000) && model.sessions[2].end == 190 &&
          model.sessions[2].tagId == 2, "shrink and retag a record");
    check(model.updateSession(2, 2, 100, 210, 1000).status == EditStatus::Overlap,
          "growing into the next record rejected");
    check(static_cast<bool>(model.updateSession(0, 3, 0, 90, 1000)) && model.sessions[0].start == 0,
          "move start earlier");
    check(model.updateSession(9, 3, 0, 90, 1000).status == EditStatus::NotFound, "unknown record index");
    check(model.updateSession(4, 3, 450, 500, 1000) && model.sessions.back().start == 450,
          "moving a record keeps start order");
    check(model.removeSession(1) && model.sessions.size() == 4 && !model.removeSession(9),
          "remove record by index");

    model.start(1, 1000);
    check(model.addSession(3, 900, 1100, 2000).status == EditStatus::Overlap,
          "record cannot overlap the running segment");
    result = model.addSession(3, 900, 1100, 2000);
    check(result.conflict == -2, "running segment conflict marker");
    check(model.updateActive(2, 950, 1200) && model.activeTagId == 2 && model.activeSince == 950,
          "running segment retagged and moved earlier");
    check(model.updateActive(2, 480, 1200).status == EditStatus::Overlap, "running start cannot overlap");
    check(model.updateActive(2, 1300, 1200).status == EditStatus::InFuture, "running start not in future");
    check(model.updateActive(77, 960, 1200).status == EditStatus::UnknownTag, "running retag needs a tag");
    check(model.stats(0, 2000, 1200).total == 90 + 90 + 100 + 50 + 250, "edited totals");
    model.stop(1200);
    check(model.updateActive(1, 1100, 1300).status == EditStatus::NotRunning, "nothing running");

    const int extra = model.addTag(L"Extra", 0x00112233u, 0);   // tags: 1(c0) 2(c1) 3(c2) extra(c0)
    check(model.moveTag(extra, -1) && model.tags[0].id == extra && model.tagIndex(1) == 3,
          "move tag up within its category");
    check(!model.moveTag(extra, -1) && !model.moveTag(99, 1) && !model.moveTag(2, 1),
          "move tag at the edge of its category or unknown");
    check(model.moveTag(extra, 1) && model.tags[3].id == extra, "move tag down within its category");
}

void flagTests(const std::filesystem::path& directory) {
    const unsigned pomodoro = tracker::SessionPomodoro;
    tracker::Model model;
    model.start(1, 100);
    model.stop(200, pomodoro);
    model.start(2, 200);
    model.stop(260);
    model.start(1, 300);
    model.stop(400, pomodoro | 8u);   // unknown bits are dropped
    check(model.sessions[0].flags == pomodoro && model.sessions[1].flags == 0 &&
          model.sessions[2].flags == pomodoro, "stop records flags");
    auto stats = model.stats(0, 1000, 1000);
    check(stats.pomodoros == 2 && stats.pomodorosByTag[1] == 2, "pomodoro counts");
    check(model.stats(0, 300, 1000).pomodoros == 1, "pomodoro counted on the day it ends");
    check(model.stats(200, 1000, 1000).pomodoros == 1, "pomodoro ending at midnight belongs to the day before");
    check(model.stats(0, 1000, 350).pomodoros == 1, "not counted before it ends");
    check(static_cast<bool>(model.updateSession(0, 2, 100, 190, 1000)) && model.sessions[0].flags == pomodoro,
          "edit keeps the pomodoro flag");
    const auto removed = model.sessions[2];
    check(model.removeSession(2) && model.stats(0, 1000, 1000).pomodoros == 1, "deleted record is uncounted");
    check(static_cast<bool>(model.addSession(removed.tagId, removed.start, removed.end, 1000, removed.flags)) &&
          model.stats(0, 1000, 1000).pomodoros == 2, "undo restores the flag");
    const auto path = directory / L"flags.dat";
    check(model.save(path.wstring()), "save flags");
    tracker::Model loaded;
    check(loaded.load(path.wstring()) && loaded.sessions.size() == 3 && loaded.sessions[0].flags == pomodoro &&
          loaded.sessions[1].flags == 0 && loaded.sessions[2].flags == pomodoro, "flags roundtrip");
    auto invalid = model;
    invalid.sessions[1].flags = 4;
    check(!invalid.save(path.wstring()), "unknown flag bits rejected on save");
}

void pomodoroTests() {
    using tracker::PomodoroPhase;
    const unsigned flag = tracker::SessionPomodoro;
    tracker::Model m;
    tracker::Pomodoro p;
    p.config = {100, 20, 50, 2, false};
    auto e = p.focus(m, 1, 1000);
    check(e.focusStarted && p.phase == PomodoroPhase::Focus && m.activeTagId == 1 && m.activeSince == 1000 &&
          p.phaseEnd == 1100, "focus starts recording");
    check(!p.tick(m, 1050, 1049).any() && p.remainingAt(1050) == 50, "countdown runs");
    e = p.tick(m, 1101, 1100);
    check(e.focusCompleted && e.breakStarted && !e.longBreak && e.restTagCreated, "focus completes into a break");
    const int rest = p.restTag;
    check(rest == 4 && m.findTag(rest)->name == L"\u4f11\u606f" &&
          m.findTag(rest)->category == 2, "rest tag created");
    check(m.sessions.size() == 1 && m.sessions[0].start == 1000 && m.sessions[0].end == 1100 &&
          m.sessions[0].flags == flag, "completed focus flagged at the exact end");
    check(m.activeTagId == rest && m.activeSince == 1100 && p.phaseEnd == 1120, "break records to the rest tag");
    e = p.tick(m, 1121, 1120);
    check(e.breakEnded && !e.focusStarted && p.phase == PomodoroPhase::Ready && !m.activeTagId &&
          m.sessions.back().tagId == rest && m.sessions.back().end == 1120 && m.sessions.back().flags == 0,
          "break ends and waits for the user");
    check(m.stats(0, 9000, 9000).pomodoros == 1, "one pomodoro counted");

    p.skip(m, 1200);
    check(p.phase == PomodoroPhase::Focus && m.activeTagId == 1 && p.phaseEnd == 1300, "skip from ready focuses");
    e = p.tick(m, 1300, 1299);
    check(e.focusCompleted && e.longBreak && p.cycle == 0 && p.phaseEnd == 1350, "every second focus: long break");
    e = p.skip(m, 1310);
    check(e.breakEnded && e.focusStarted && p.phase == PomodoroPhase::Focus && m.activeTagId == 1 &&
          m.activeSince == 1310 && m.sessions.back().tagId == rest && m.sessions.back().end == 1310,
          "skipping a break starts the next focus");

    p.pause(m, 1330);
    check(p.paused && !m.activeTagId && p.remainingAt(5000) == 80 && m.sessions.back().end == 1330 &&
          m.sessions.back().flags == 0, "pause records the partial focus");
    check(!p.tick(m, 1400, 1399).any() && p.paused, "paused countdown holds");
    p.resume(m, 1500);
    check(!p.paused && m.activeTagId == 1 && m.activeSince == 1500 && p.phaseEnd == 1580, "resume continues");

    p.focus(m, 2, 1540);
    check(m.activeTagId == 2 && p.focusTag == 2 && p.phaseEnd == 1580 && m.sessions.back().end == 1540,
          "switching tags keeps the countdown");
    e = p.tick(m, 1580, 1579);
    check(e.focusCompleted && m.sessions.back().tagId == 2 && m.sessions.back().start == 1540 &&
          m.sessions.back().flags == flag, "the final segment is the pomodoro");
    e = p.focus(m, 1, 1590);
    check(e.focusStarted && p.phase == PomodoroPhase::Focus && m.sessions.back().tagId == rest &&
          m.sessions.back().end == 1590 && m.activeTagId == 1, "a tag click during a break starts focusing");

    const int counted = m.stats(0, 9000, 9000).pomodoros;
    p.abandon(m, 1620);
    check(p.phase == PomodoroPhase::Ready && !m.activeTagId && m.sessions.back().end == 1620 &&
          m.stats(0, 9000, 9000).pomodoros == counted, "abandon keeps the time, counts nothing");

    p.focus(m, 1, 2000);
    e = p.tick(m, 2600, 2040);
    check(e.interrupted && p.phase == PomodoroPhase::Ready && !m.activeTagId && m.sessions.back().end == 2040 &&
          m.sessions.back().flags == 0, "sleep interrupts the focus at the last tick");

    p.focus(m, 1, 3000);
    e = p.tick(m, 3200, 3090);
    const auto& focusRecord = m.sessions[m.sessions.size() - 2];
    check(e.focusCompleted && e.breakEnded && p.phase == PomodoroPhase::Ready && !m.activeTagId &&
          focusRecord.end == 3100 && focusRecord.flags == flag && m.sessions.back().start == 3100 &&
          m.sessions.back().end == 3150, "a late tick catches up with focus and break");

    p.config.autoNext = true;
    p.focus(m, 1, 4000);
    p.tick(m, 4100, 4099);
    e = p.tick(m, 4120, 4119);
    check(e.breakEnded && e.focusStarted && p.phase == PomodoroPhase::Focus && m.activeTagId == 1 &&
          m.activeSince == 4120 && p.phaseEnd == 4220, "auto-start after a break");
    p.config.autoNext = false;

    m.stop(4150);
    p.tick(m, 4160, 4159);
    check(p.paused && p.remainingAt(9999) == 60, "a record stopped elsewhere pauses the countdown");
    p.abandon(m, 4170);
    check(p.phase == PomodoroPhase::Ready && !m.activeTagId, "abandon while paused");

    m.start(3, 5000);           // a free-running record when focus begins is split
    p.focus(m, 3, 5100);
    check(m.sessions.back().tagId == 3 && m.sessions.back().start == 5000 && m.sessions.back().end == 5100 &&
          m.activeSince == 5100, "free record split at focus start");
    p.leave();
    check(p.phase == PomodoroPhase::Ready && m.activeTagId == 3, "leaving pomodoro mode keeps recording");

    tracker::Model fresh;
    fresh.addTag(L"\u4f11\u606f", 0x123456u, 1);
    tracker::Pomodoro q;
    q.config = {10, 5, 5, 4, false};
    q.focus(fresh, 1, 100);
    e = q.tick(fresh, 110, 109);
    check(q.restTag == 4 && fresh.activeTagId == 4 && fresh.tags.size() == 4 && !e.restTagCreated,
          "an existing rest tag is reused");
    check(fresh.findTag(1) && !q.focus(fresh, 99, 120).any(), "unknown tag ignored");
}

bool sameHue(unsigned a, unsigned b) {
    const double d = std::abs(tracker::ToOklch(a).h - tracker::ToOklch(b).h);
    return std::min(d, 2 * 3.14159265358979 - d) < 0.12;
}

void categoryTests(const std::filesystem::path& directory) {
    tracker::Model m;
    check(m.categories.size() == 3 && m.findCategory(0)->name == L"\u5de5\u4f5c" && m.categoryIndex(2) == 2,
          "default categories");
    const int study = m.addCategory(L"\u5b66\u4e60", 0x00D65062u);
    check(study == 3 && m.findCategory(study)->color == 0x00D65062u, "add category");
    check(m.addCategory(L"", 0) == -1 && m.addCategory(L"Bad", 0x01000000u) == -1, "invalid category rejected");
    check(m.addTag(L"Lost", 0, 42) == 0, "tag needs an existing category");

    const auto family = tracker::ColorFamily(0x00D6782Au);
    check(family.size() == 8 && std::count(family.begin(), family.end(), 0x00D6782Au) == 1 &&
          tracker::ToOklch(family.front()).L < tracker::ToOklch(family.back()).L, "family: base included, deep to light");
    check(std::all_of(family.begin(), family.end(), [](unsigned c) { return sameHue(c, 0x00D6782Au); }) &&
          std::set<unsigned>(family.begin(), family.end()).size() == 8, "family keeps the hue, all distinct");
    const auto grey = tracker::ColorFamily(0x00808080u);
    check(std::set<unsigned>(grey.begin(), grey.end()).size() == 8, "a grey family varies lightness");

    const int a = m.addTag(L"\u5199\u7814\u62a5", m.nextTagColor(0), 0);
    const int b = m.addTag(L"\u5f00\u4f1a", m.nextTagColor(0), 0);
    const unsigned work = m.findCategory(0)->color;
    check(a && b && m.findTag(a)->color != work && m.findTag(b)->color != work &&
          m.findTag(a)->color != m.findTag(b)->color && sameHue(m.findTag(a)->color, work) &&
          sameHue(m.findTag(b)->color, work), "new tags take unused shades of the category colour");
    check(m.nextTagColor(study) == 0x00D65062u, "first tag of a category gets the category colour");

    m.start(a, 100);
    m.start(b, 160);
    m.start(2, 200);
    m.stop(260);
    const auto stats = m.stats(0, 1000, 1000);
    check(stats.ofCategory(0) == 100 && stats.ofCategory(1) == 60 && stats.ofCategory(study) == 0 &&
          stats.ofTag(a) == 60 && stats.total == 160, "stats by category");

    // tags: 1(c0) 2(c1) 3(c2) a(c0) b(c0)
    check(m.moveTag(b, -1) && m.tags[3].id == b && m.tags[4].id == a, "move tag within its category");
    check(m.moveTag(b, -1) && m.tags[0].id == b && m.tags[3].id == 1, "moving skips other categories");
    check(!m.moveTag(b, -1) && !m.moveTag(2, 1) && !m.moveTag(2, -1), "edges of a category");

    check(m.updateCategory(0, L"Work", 0x003468EBu) && !m.updateCategory(99, L"X", 0), "update category");
    m.recolorCategory(0);
    std::vector<unsigned> colors;
    for (const auto& tag : m.tags) if (tag.category == 0) colors.push_back(tag.color);
    check(colors.size() == 3 && colors[0] == 0x003468EBu && std::set<unsigned>(colors.begin(), colors.end()).size() == 3 &&
          std::all_of(colors.begin(), colors.end(), [](unsigned c) { return sameHue(c, 0x003468EBu); }),
          "recolouring gives the category's tags distinct shades, the first its colour");

    check(m.moveCategory(study, -1) && m.categories[2].id == study && !m.moveCategory(0, -1), "reorder categories");
    const int maths = m.addTag(L"Maths", m.nextTagColor(study), study);
    check(!m.removeCategory(study, study) && !m.removeCategory(study, 99) && !m.removeCategory(99, 0),
          "removal needs another existing category");
    check(m.removeCategory(study, 2) && !m.findCategory(study) && m.findTag(maths)->category == 2,
          "removing a category moves its tags");
    tracker::Model one;
    check(one.removeCategory(1, 0) && one.removeCategory(2, 0) && !one.removeCategory(0, 0) &&
          one.categories.size() == 1 && one.tagsIn(0) == 3, "the last category stays");

    const auto path = directory / L"categories.dat";
    check(m.save(path.wstring()), "save categories");
    tracker::Model loaded;
    check(loaded.load(path.wstring()) && loaded.categories.size() == m.categories.size() &&
          loaded.categories[0].name == L"Work" && loaded.categories[0].color == 0x003468EBu &&
          loaded.findTag(maths)->category == 2 && loaded.tags.size() == m.tags.size(), "categories roundtrip");
    auto invalid = m;
    invalid.tags[0].category = 42;
    check(!invalid.save(path.wstring()), "tag with a missing category rejected on save");
    invalid = m;
    invalid.categories.push_back(invalid.categories[0]);
    check(!invalid.save(path.wstring()), "duplicate category id rejected on save");
}

// statsBetween() and hoursByCategory(): the per-range numbers the statistics page draws.
void rangeTests() {
    tracker::Model m;
    const int game = m.addTag(L"\u6e38\u620f", m.nextTagColor(1), 1);
    const long long now = 100000;
    check(m.addSession(1, 100, 400, now).status == tracker::EditStatus::Ok, "range fixture 1");
    check(m.addSession(game, 500, 700, now).status == tracker::EditStatus::Ok, "range fixture 2");
    check(m.addSession(1, 800, 1000, now, tracker::SessionPomodoro).status == tracker::EditStatus::Ok, "range fixture 3");
    m.start(3, 1100);
    const std::vector<long long> bounds{0, 600, 1000, 2000};
    const auto ranges = m.statsBetween(bounds, 1300);
    check(ranges.size() == 3, "one result per range");
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        const auto single = m.stats(bounds[i], bounds[i + 1], 1300);
        check(ranges[i].total == single.total && ranges[i].byTag == single.byTag &&
              ranges[i].byCategory == single.byCategory && ranges[i].pomodoros == single.pomodoros,
              "statsBetween matches stats per range");
    }
    check(ranges[0].ofTag(game) == 100 && ranges[1].ofTag(game) == 100, "a record split at a boundary");
    check(ranges[1].pomodoros == 1 && ranges[2].pomodoros == 0, "a pomodoro counts where it ended");
    check(ranges[2].ofCategory(2) == 200, "the running segment up to now");
    check(m.statsBetween({0}, now).empty() && m.statsBetween({}, now).empty(), "no ranges");
    check(m.statsBetween({5000, 6000}, 1300).at(0).total == 0, "a range after every record");
    check(m.statsBetween({5000, 6000}, now).at(0).ofCategory(2) == 1000, "the running segment fills later ranges");
    check(m.statsBetween({0, 600}, 300).at(0).total == 200, "clipped to now");

    // hours: two 24-hour days (86400 s), a day of 23 hours spread evenly, the running segment
    tracker::Model h;
    const long long day = 86400;
    check(h.addSession(1, 3600 + 1800, 2 * 3600 + 600, 10 * day).status == tracker::EditStatus::Ok, "hour fixture 1");
    check(h.addSession(2, day - 1800, day + 1800, 10 * day).status == tracker::EditStatus::Ok, "hour fixture 2");
    const auto hours = h.hoursByCategory({0, day, 2 * day}, 10 * day);
    check(hours.at(0)[1] == 1800 && hours.at(0)[2] == 600 && hours.at(0)[0] == 0, "work split at the hour");
    check(hours.at(1)[23] == 1800 && hours.at(1)[0] == 1800, "a record across midnight");
    long long sum = 0;
    for (const auto& entry : hours) for (long long seconds : entry.second) sum += seconds;
    check(sum == 2400 + 3600, "every second lands in one hour");
    tracker::Model dst;
    const long long shortDay = 23 * 3600;
    check(dst.addSession(1, 0, shortDay, 10 * day).status == tracker::EditStatus::Ok, "dst fixture");
    const auto spread = dst.hoursByCategory({0, shortDay}, 10 * day);
    long long dstSum = 0;
    bool even = true;
    for (long long seconds : spread.at(0)) { dstSum += seconds; even = even && seconds >= 3449 && seconds <= 3451; }
    check(dstSum == shortDay && even, "a 23-hour day spreads over 24 hours");
    tracker::Model running;
    running.start(1, 7200);
    check(running.hoursByCategory({0, day}, 7200 + 900).at(0)[2] == 900, "running segment by hour");
    check(running.hoursByCategory({0}, 9000).empty(), "no days, no hours");
}

// Category free flags and the guard settings: defaults, validation, persistence, pruning.
void guardModelTests(const std::filesystem::path& directory) {
    tracker::Model m;
    check(m.isFree(1) && !m.isFree(0) && !m.isFree(2), "play is free time by default");
    check(!m.guard.enabled && m.guard == tracker::DefaultGuardConfig() && !m.guard.rules.empty() &&
          !m.guard.keywords.empty(), "guard defaults");
    const int study = m.addCategory(L"\u5b66\u4e60", 0x00D65062u);
    check(study >= 0 && !m.isFree(study), "new categories follow the whitelist");
    check(m.setCategoryFree(study, true) && m.isFree(study) && m.setCategoryFree(study, false) && !m.isFree(study),
          "toggle free time");
    check(!m.setCategoryFree(99, true), "unknown category has no flag");
    auto config = m.guard;
    config.enabled = true;
    config.graceSeconds = 0;
    config.repeatSeconds = 10;
    config.releaseMinutes = 240;
    config.idleSeconds = 30;
    config.options = tracker::GuardMinimize | tracker::GuardStrict;
    config.shieldSeconds = 0;
    config.dailyEscapes = 20;
    config.rules.push_back({L"*", tracker::GuardMode::AllowOnly, {L"\u98de\u4e66", L"GitHub"}, {0, study}});
    config.rules.push_back({L"wechat.exe", tracker::GuardMode::CheckTitle, {}, {study}});
    config.keywords.push_back(L"\u76f4\u64ad");
    check(m.setGuard(config) && m.guard == config && m.guard.strict(), "valid guard settings accepted");
    const auto rejected = [&](tracker::GuardConfig bad, const char* what) { check(!m.setGuard(bad), what); };
    auto bad = config; bad.graceSeconds = 121; rejected(bad, "grace range");
    bad = config; bad.repeatSeconds = 9; rejected(bad, "repeat range");
    bad = config; bad.releaseMinutes = 0; rejected(bad, "release range");
    bad = config; bad.idleSeconds = 3601; rejected(bad, "idle range");
    bad = config; bad.options = 16; rejected(bad, "unknown option");
    bad = config; bad.shieldSeconds = 121; rejected(bad, "shield delay range");
    bad = config; bad.dailyEscapes = 21; rejected(bad, "daily escapes range");
    bad = config; bad.rules.back().exe = L"WeChat.exe"; rejected(bad, "program names are stored lowercase");
    bad = config; bad.rules.back().exe = L"c:\\apps\\wechat.exe"; rejected(bad, "no paths");
    bad = config; bad.rules.back().exe = L""; rejected(bad, "program name required");
    bad = config; bad.rules.back().keywords = {L"x"}; rejected(bad, "only AllowOnly rules have keywords");
    bad = config; bad.rules[bad.rules.size() - 2].keywords.clear(); rejected(bad, "AllowOnly needs keywords");
    bad = config; bad.rules.back().scope = {42}; rejected(bad, "unknown scope category");
    bad = config; bad.rules.back().scope = {study, study}; rejected(bad, "duplicate scope category");
    bad = config; bad.keywords.push_back(L"a\tb"); rejected(bad, "control characters");
    bad = config; bad.keywords.push_back(std::wstring(300, L'x')); rejected(bad, "keyword length");
    bad = config; bad.rules.resize(257, bad.rules.front()); rejected(bad, "rule count");
    check(m.guard == config, "rejected settings leave the guard unchanged");
    const auto path = directory / L"guard.dat";
    check(m.save(path.wstring()), "save guard settings");
    tracker::Model loaded;
    check(loaded.load(path.wstring()) && loaded.guard == config && loaded.isFree(1) && !loaded.isFree(study),
          "guard settings and free flags roundtrip");
    writeBytes(directory / L"guard5.dat", downgrade(readBytes(path), '5'));
    auto older = config;
    older.shieldSeconds = tracker::GuardConfig{}.shieldSeconds;
    older.dailyEscapes = tracker::GuardConfig{}.dailyEscapes;
    check(loaded.load((directory / L"guard5.dat").wstring()) && loaded.guard == older && loaded.isFree(1),
          "TRACKER5 guard settings load with the strict mode defaults");
    check(loaded.setCategoryFree(study, true) && loaded.save(path.wstring()) && m.load(path.wstring()) &&
          m.isFree(study), "free flag roundtrip");
    check(loaded.removeCategory(study, 0), "remove a scoped category");
    const auto& rules = loaded.guard.rules;
    check(std::none_of(rules.begin(), rules.end(), [](const tracker::GuardRule& r) { return r.exe == L"wechat.exe"; }),
          "a rule for a removed category alone goes");
    const auto any = std::find_if(rules.begin(), rules.end(), [](const tracker::GuardRule& r) { return r.exe == L"*"; });
    check(any != rules.end() && any->scope == std::vector<int>{0}, "other scopes are pruned");
    check(loaded.save(path.wstring()) && m.load(path.wstring()) && m.guard == loaded.guard, "pruned settings save");
}

void persistenceTests(const std::filesystem::path& directory) {
    const auto path = directory / L"\u65f6\u95f4-\u8bb0\u5f55.bin";
    const auto corruptPath = directory / L"corrupt.bin";
    tracker::Model original;
    const std::wstring unicode = L"\u4e13\u6ce8 \U0001F345 \u00e9\n\"\t";
    const int id = original.addTag(unicode, 0x00ABCDEFu, 2);
    original.start(1, 100);
    original.start(id, 200);
    original.stop(300);
    original.start(2, 400);
    check(original.save(path.wstring()), "save Unicode path and names");
    tracker::Model loaded;
    check(loaded.load(path.wstring()), "load persisted model");
    check(loaded.findTag(id) && loaded.findTag(id)->name == unicode &&
          loaded.findTag(id)->color == 0x00ABCDEFu, "Unicode and COLORREF roundtrip");
    check(loaded.sessions.size() == 2 && loaded.activeTagId == 2 && loaded.activeSince == 400,
          "sessions and running state roundtrip");
    check(loaded.stats(0, 1000, 450).total == 250, "reopened active timer continues");
    loaded.stop(500);
    check(loaded.save(path.wstring()), "atomic replacement of existing file");
    tracker::Model stopped;
    check(stopped.load(path.wstring()) && stopped.activeTagId == 0 && stopped.sessions.size() == 3,
          "stopped state roundtrip");
    check(original.save(path.wstring()), "restore fixture");
    const auto valid = readBytes(path);
    check(valid.size() > 8 && valid[0] == 'T' && valid[7] == '6', "versioned persistence");
    std::vector<std::size_t> categoryOffsets, tagOffsets;
    std::size_t cursor = 12;
    for (std::uint64_t i = 0; i < number(valid, 8, 4); ++i) {
        categoryOffsets.push_back(cursor);
        cursor += 16 + static_cast<std::size_t>(number(valid, cursor + 12, 4));
    }
    const auto tagCountOffset = cursor;
    cursor += 4;
    for (std::uint64_t i = 0; i < number(valid, tagCountOffset, 4); ++i) {
        tagOffsets.push_back(cursor);
        cursor += 16 + static_cast<std::size_t>(number(valid, cursor + 12, 4));
    }
    const auto countOffset = cursor;
    const auto sessionOffset = cursor + 4;
    const auto activeOffset = sessionOffset + 24 * number(valid, countOffset, 4);
    const auto guardOffset = activeOffset + 20;     // enabled, seven numbers, rules, keywords
    const auto rejects = [&](std::vector<unsigned char> bytes) {
        writeBytes(corruptPath, bytes);
        check(!loaded.load(corruptPath.wstring()), "corrupt load rejected");
        check(loaded.activeTagId == 0 && loaded.sessions.size() == 3 && loaded.findTag(id)->name == unicode,
              "failed load leaves model unchanged");
    };
    auto bad = valid; bad[7] = '7'; rejects(bad);
    bad = valid; put(bad, activeOffset + 12, ~std::uint64_t(0), 8); rejects(bad);
    bad = valid; put(bad, activeOffset + 12, 253402300800ULL, 8); rejects(bad);
    bad = valid; bad.push_back(0); rejects(bad);
    for (const auto length : {std::size_t(0), std::size_t(7), std::size_t(11),
                              valid.size() / 2, valid.size() - 1})
        rejects(std::vector<unsigned char>(valid.begin(), valid.begin() + length));
    bad = valid; put(bad, 8, 0, 4); rejects(bad);
    bad = valid; put(bad, 8, 65, 4); rejects(bad);
    bad = valid; put(bad, categoryOffsets[1], 0, 4); rejects(bad);
    bad = valid; put(bad, categoryOffsets[0] + 4, 0xFFFFFFFFu, 4); rejects(bad);
    bad = valid; put(bad, categoryOffsets[0] + 8, 2, 4); rejects(bad);         // unknown category flag
    bad = valid; put(bad, categoryOffsets[0] + 12, 0, 4); rejects(bad);
    bad = valid; put(bad, guardOffset, 2, 4); rejects(bad);                    // enabled is 0 or 1
    bad = valid; put(bad, guardOffset + 4, 121, 4); rejects(bad);              // grace
    bad = valid; put(bad, guardOffset + 8, 9, 4); rejects(bad);                // repeat
    bad = valid; put(bad, guardOffset + 12, 0, 4); rejects(bad);               // release minutes
    bad = valid; put(bad, guardOffset + 16, 29, 4); rejects(bad);              // idle
    bad = valid; put(bad, guardOffset + 20, 16, 4); rejects(bad);              // unknown option
    bad = valid; put(bad, guardOffset + 24, 121, 4); rejects(bad);             // shield delay
    bad = valid; put(bad, guardOffset + 28, 21, 4); rejects(bad);              // daily escapes
    bad = valid; put(bad, guardOffset + 32, 257, 4); rejects(bad);             // rule count
    bad = valid; put(bad, guardOffset + 36, 0, 4); rejects(bad);               // empty program name
    bad = valid; put(bad, guardOffset + 40 + number(valid, guardOffset + 36, 4), 3, 4); rejects(bad);   // mode
    bad = valid; put(bad, tagCountOffset, 4097, 4); rejects(bad);
    bad = valid; put(bad, countOffset, 1000001, 4); rejects(bad);
    bad = valid; put(bad, tagOffsets[0], 0, 4); rejects(bad);
    bad = valid; put(bad, tagOffsets[1], 1, 4); rejects(bad);
    bad = valid; put(bad, tagOffsets[0] + 4, 0xFFFFFFFFu, 4); rejects(bad);
    bad = valid; put(bad, tagOffsets[0] + 8, 3, 4); rejects(bad);
    bad = valid; put(bad, tagOffsets[0] + 12, 4097, 4); rejects(bad);
    bad = valid; put(bad, tagOffsets[0] + 12, 0, 4); rejects(bad);
    bad = valid; bad[tagOffsets[0] + 16] = 0xFF; rejects(bad);
    bad = valid; bad[tagOffsets[0] + 16] = 0; rejects(bad);
    bad = valid; bad[tagOffsets[0] + 16] = 0xC0; bad[tagOffsets[0] + 17] = 0xAF; rejects(bad);
    bad = valid; bad[tagOffsets[0] + 16] = 0xED; bad[tagOffsets[0] + 17] = 0xA0;
    bad[tagOffsets[0] + 18] = 0x80; rejects(bad);
    bad = valid; put(bad, sessionOffset, 999, 4); rejects(bad);
    bad = valid; put(bad, sessionOffset + 12, 100, 8); rejects(bad);
    bad = valid; put(bad, sessionOffset + 4, ~std::uint64_t(0), 8); rejects(bad);
    bad = valid; put(bad, sessionOffset + 28, 150, 8); rejects(bad);
    bad = valid; put(bad, sessionOffset + 20, 2, 4); rejects(bad);
    bad = valid; put(bad, activeOffset, 999, 4); rejects(bad);
    bad = valid; put(bad, activeOffset + 4, 250, 8); rejects(bad);
    bad = valid; put(bad, activeOffset, 0, 4); rejects(bad);
    bad = valid; put(bad, activeOffset + 4, ~std::uint64_t(0), 8); rejects(bad);

    for (const char version : {'5', '4', '3', '2', '1'}) {
        writeBytes(corruptPath, downgrade(valid, version));
        tracker::Model old;
        check(old.load(corruptPath.wstring()) && old.sessions.size() == 2 && old.activeTagId == 2 &&
              old.sessions[1].tagId == id && (version >= '4' || old.sessions[1].flags == 0) &&
              old.categories.size() == 3 && old.findTag(id)->category == 2, "older formats with records load");
        check(old.isFree(1) && !old.isFree(0) && !old.isFree(2) && old.guard == tracker::DefaultGuardConfig() &&
              !old.guard.enabled, "older formats get the guard defaults");
        check(old.lastSaved == (version >= '2' ? static_cast<long long>(number(valid, activeOffset + 12, 8)) : 0),
              "older format checkpoint");
    }
    check(!loaded.load((directory / L"missing.bin").wstring()), "missing load fails");
    check(!original.save((directory / L"missing" / L"file.bin").wstring()), "missing parent save fails");
    check(!original.save(directory.wstring()), "directory replacement fails safely");
    auto invalid = original;
    invalid.sessions.push_back({1, 150, 250});
    check(!invalid.save(path.wstring()), "overlapping public records cannot be saved");
    check(readBytes(path) == valid, "failed save preserves existing file");
    invalid = original; invalid.tags.push_back(invalid.tags[0]);
    check(!invalid.save(path.wstring()), "duplicate public tag ID rejected");
    invalid = original; invalid.activeSince = 299;
    check(!invalid.save(path.wstring()), "active overlap rejected on save");
    invalid = original; invalid.sessions[0].tagId = 999;
    check(!invalid.save(path.wstring()), "unknown session reference rejected on save");
    tracker::Model empty;
    empty.tags.clear();
    check(empty.save(corruptPath.wstring()) && stopped.load(corruptPath.wstring()) && stopped.tags.empty(),
          "empty model roundtrip");
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        check(entry.path().filename().wstring().find(L".tmp-") == std::wstring::npos,
              "temporary save files cleaned up");
}
}

int main() {
    try {
        TempFiles files;
        timingTests();
        tagTests();
        editingTests();
        pomodoroTests();
        persistenceTests(files.directory);
        flagTests(files.directory);
        categoryTests(files.directory);
        guardModelTests(files.directory);
        rangeTests();
        tracker::Model recovery;
        recovery.start(1, 100);
        const auto recoveryPath = files.directory / L"recovery.dat";
        check(recovery.save(recoveryPath.wstring(), 160), "save explicit checkpoint");
        check(recovery.load(recoveryPath.wstring()) && recovery.lastSaved == 160, "checkpoint roundtrip");
        check(!recovery.needsRecovery(280) && recovery.needsRecovery(281), "recovery threshold");
        auto continued = recovery;
        continued.resolveRecovery(tracker::Recovery::Continue);
        check(continued.activeSince == 100, "continue retains active segment");
        auto discarded = recovery;
        discarded.resolveRecovery(tracker::Recovery::Discard);
        check(!discarded.activeTagId && discarded.sessions.empty(), "discard only open segment");
        recovery.resolveRecovery(tracker::Recovery::EndAtLastSaved);
        check(!recovery.activeTagId && recovery.sessions.back().end == 160, "recover to checkpoint");
        writeBytes(recoveryPath, downgrade(readBytes(recoveryPath), '1'));
        check(recovery.load(recoveryPath.wstring()) && recovery.lastSaved == 0, "legacy has unknown checkpoint");
        check(recovery.needsRecovery(200), "legacy open segment requires decision");
        recovery.resolveRecovery(tracker::Recovery::EndAtLastSaved);
        check(recovery.sessions.empty() && !recovery.activeTagId, "unknown checkpoint never fabricated");
        std::cout << "PASS: " << checks << " checks\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
