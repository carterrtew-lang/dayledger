#include "../src/tracker_model.h"

#include <cstdlib>
#include <iostream>
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
    check(stats.work == 60 && stats.play == 40 && stats.other == 0 && stats.total == 100,
          "switch and stop totals");
    check(stats.byTag[1] == 60 && stats.byTag[2] == 40, "per-tag totals");
    check(model.activeTagId == 0 && model.activeSince == 0 && model.sessions.size() == 2,
          "stop is idempotent");
    model.start(3, 250);
    check(model.stats(0, 400, 275).other == 25, "active segment counted");
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
    check(first.work == 30 && first.total == 30, "previous day intersection");
    check(second.work == 30 && second.play == 60 && second.total == 90, "next day and active intersection");
    midnight.stop(86520);
    check(midnight.stats(86400, 86460, 90000).total == 60, "arbitrary day boundary intersection");
}

void tagTests() {
    tracker::Model model;
    const int id = model.addTag(L"\u9605\u8bfb", 0x00563412u, tracker::Category::Other);
    check(id == 4 && model.findTag(id)->color == 0x00563412u, "add tag preserves COLORREF");
    check(model.updateTag(id, L"Reading", 0x00112233u, tracker::Category::Work), "update tag");
    check(model.findTag(id)->category == tracker::Category::Work, "updated category");
    check(!model.updateTag(999, L"Missing", 0, tracker::Category::Work), "unknown update fails");
    check(model.addTag(L"", 0, tracker::Category::Work) == 0, "empty tag rejected");
    check(model.addTag(L"Bad", 0xFF000000u, tracker::Category::Work) == 0, "invalid color rejected");
    check(model.addTag(L"Bad", 0, static_cast<tracker::Category>(3)) == 0, "invalid category rejected");
    check(model.addTag(std::wstring(4097, L'x'), 0, tracker::Category::Work) == 0, "oversize name rejected");
    check(model.addTag(std::wstring(1, wchar_t(0xD800)), 0, tracker::Category::Work) == 0,
          "unpaired surrogate rejected");
    check(model.addTag(std::wstring(L"a\0b", 3), 0, tracker::Category::Work) == 0, "embedded NUL rejected");
    model.start(id, 10);
    check(!model.removeTag(id), "active tag cannot be removed");
    model.stop(20);
    check(!model.removeTag(id), "referenced tag cannot be removed");
    check(model.updateTag(id, L"Renamed", 0, tracker::Category::Play), "referenced tag editable");
    check(model.stats(0, 30, 30).play == 10, "history uses current tag category");
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

    check(model.moveTag(3, -1) && model.tags[1].id == 3 && model.tagIndex(3) == 1, "move tag up");
    check(!model.moveTag(1, -1) && !model.moveTag(99, 1), "move tag at edge or unknown");
    check(model.moveTag(3, 1) && model.tags[2].id == 3, "move tag down");
}

void persistenceTests(const std::filesystem::path& directory) {
    const auto path = directory / L"\u65f6\u95f4-\u8bb0\u5f55.bin";
    const auto corruptPath = directory / L"corrupt.bin";
    tracker::Model original;
    const std::wstring unicode = L"\u4e13\u6ce8 \U0001F345 \u00e9\n\"\t";
    const int id = original.addTag(unicode, 0x00ABCDEFu, tracker::Category::Other);
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
    check(valid.size() > 8 && valid[0] == 'T' && valid[7] == '2', "versioned persistence");
    std::vector<std::size_t> tagOffsets;
    std::size_t cursor = 12;
    for (std::uint64_t i = 0; i < number(valid, 8, 4); ++i) {
        tagOffsets.push_back(cursor);
        cursor += 16 + static_cast<std::size_t>(number(valid, cursor + 12, 4));
    }
    const auto countOffset = cursor;
    const auto sessionOffset = cursor + 4;
    const auto activeOffset = sessionOffset + 20 * number(valid, countOffset, 4);
    const auto rejects = [&](std::vector<unsigned char> bytes) {
        writeBytes(corruptPath, bytes);
        check(!loaded.load(corruptPath.wstring()), "corrupt load rejected");
        check(loaded.activeTagId == 0 && loaded.sessions.size() == 3 && loaded.findTag(id)->name == unicode,
              "failed load leaves model unchanged");
    };
    auto bad = valid; bad[7] = '3'; rejects(bad);
    bad = valid; put(bad, activeOffset + 12, ~std::uint64_t(0), 8); rejects(bad);
    bad = valid; put(bad, activeOffset + 12, 253402300800ULL, 8); rejects(bad);
    bad = valid; bad.push_back(0); rejects(bad);
    for (const auto length : {std::size_t(0), std::size_t(7), std::size_t(11),
                              valid.size() / 2, valid.size() - 1})
        rejects(std::vector<unsigned char>(valid.begin(), valid.begin() + length));
    bad = valid; put(bad, 8, 4097, 4); rejects(bad);
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
    bad = valid; put(bad, sessionOffset + 24, 150, 8); rejects(bad);
    bad = valid; put(bad, activeOffset, 999, 4); rejects(bad);
    bad = valid; put(bad, activeOffset + 4, 250, 8); rejects(bad);
    bad = valid; put(bad, activeOffset, 0, 4); rejects(bad);
    bad = valid; put(bad, activeOffset + 4, ~std::uint64_t(0), 8); rejects(bad);

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
        persistenceTests(files.directory);
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
        auto legacy = readBytes(recoveryPath);
        legacy[7] = '1';
        legacy.resize(legacy.size() - 8);
        writeBytes(recoveryPath, legacy);
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
