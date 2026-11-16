#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tracker {

enum class Category { Work = 0, Play = 1, Other = 2 };
struct Tag { int id; std::wstring name; unsigned color; Category category; };
struct Session { int tagId; long long start; long long end; };
struct DayStats {
    long long work = 0, play = 0, other = 0, total = 0;
    std::map<int, long long> byTag;
};

enum class Recovery { EndAtLastSaved, Continue, Discard };

// Outcome of a record edit. `conflict` names the overlapping record (an index into
// Model::sessions), or -2 for the running segment, when status is Overlap.
enum class EditStatus { Ok, UnknownTag, InvalidRange, InFuture, Overlap, NotFound, NotRunning };
struct EditResult {
    EditStatus status = EditStatus::Ok;
    int conflict = -1;
    explicit operator bool() const { return status == EditStatus::Ok; }
};

class Model {
public:
    std::vector<Tag> tags;
    std::vector<Session> sessions;   // kept sorted by start, never overlapping
    int activeTagId = 0;
    long long activeSince = 0;
    long long lastSaved = 0;

    // COLORREF values (0x00BBGGRR) of the default work / play / other hues.
    Model() : tags{{1, L"\u5de5\u4f5c", 0x00D6782Au, Category::Work},
                   {2, L"\u5a31\u4e50", 0x003468EBu, Category::Play},
                   {3, L"\u5176\u4ed6", 0x007AAF1Bu, Category::Other}} {}

    const Tag* findTag(int id) const {
        for (const auto& tag : tags) if (tag.id == id) return &tag;
        return nullptr;
    }

    int tagIndex(int id) const {
        for (std::size_t i = 0; i < tags.size(); ++i) if (tags[i].id == id) return static_cast<int>(i);
        return -1;
    }

    // Moves a tag one place up (delta < 0) or down (delta > 0); the order is persisted.
    bool moveTag(int id, int delta) {
        const int from = tagIndex(id);
        const int to = from + (delta < 0 ? -1 : 1);
        if (from < 0 || delta == 0 || to < 0 || to >= static_cast<int>(tags.size())) return false;
        std::swap(tags[static_cast<std::size_t>(from)], tags[static_cast<std::size_t>(to)]);
        return true;
    }

    std::size_t sessionCount(int tagId) const {
        return static_cast<std::size_t>(std::count_if(sessions.begin(), sessions.end(),
            [tagId](const Session& session) { return session.tagId == tagId; }));
    }

    // The tag of the running segment, otherwise of the most recent record (0 when none).
    int lastUsedTag() const {
        if (activeTagId) return activeTagId;
        return sessions.empty() ? 0 : sessions.back().tagId;
    }

    // Validates [start, end) for a record of tagId against every record except `ignore`
    // and against the running segment. Records may not reach past `now`.
    EditResult checkRange(int tagId, long long start, long long end, long long now,
                          long long ignore = -1) const {
        if (!findTag(tagId)) return {EditStatus::UnknownTag};
        if (!validTime(start) || !validTime(end) || end <= start) return {EditStatus::InvalidRange};
        if (validTime(now) && end > now) return {EditStatus::InFuture};
        for (std::size_t i = 0; i < sessions.size(); ++i) {
            if (static_cast<long long>(i) == ignore) continue;
            if (start < sessions[i].end && sessions[i].start < end)
                return {EditStatus::Overlap, static_cast<int>(i)};
        }
        if (activeTagId && end > activeSince) return {EditStatus::Overlap, -2};
        return {};
    }

    EditResult addSession(int tagId, long long start, long long end, long long now) {
        const auto result = checkRange(tagId, start, end, now);
        if (!result) return result;
        if (sessions.size() >= maxSessions) return {EditStatus::InvalidRange};
        insertSorted({tagId, start, end});
        return {};
    }

    EditResult updateSession(std::size_t index, int tagId, long long start, long long end,
                             long long now) {
        if (index >= sessions.size()) return {EditStatus::NotFound};
        const auto result = checkRange(tagId, start, end, now, static_cast<long long>(index));
        if (!result) return result;
        sessions.erase(sessions.begin() + static_cast<std::ptrdiff_t>(index));
        insertSorted({tagId, start, end});
        return {};
    }

    bool removeSession(std::size_t index) {
        if (index >= sessions.size()) return false;
        sessions.erase(sessions.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    // Re-labels and/or re-times the running segment. It must still start after every record.
    EditResult updateActive(int tagId, long long since, long long now) {
        if (!activeTagId) return {EditStatus::NotRunning};
        if (!findTag(tagId)) return {EditStatus::UnknownTag};
        if (!validTime(since)) return {EditStatus::InvalidRange};
        if (validTime(now) && since > now) return {EditStatus::InFuture};
        for (std::size_t i = 0; i < sessions.size(); ++i)
            if (sessions[i].end > since) return {EditStatus::Overlap, static_cast<int>(i)};
        activeTagId = tagId;
        activeSince = since;
        return {};
    }

    int addTag(const std::wstring& name, unsigned color, Category category) {
        if (tags.size() >= maxTags || !validTagValue(name, color, category)) return 0;
        int id = 0;
        for (const auto& tag : tags) id = (std::max)(id, tag.id);
        if (id == (std::numeric_limits<int>::max)()) return 0;
        tags.push_back({++id, name, color, category});
        return id;
    }

    bool updateTag(int id, const std::wstring& name, unsigned color, Category category) {
        if (!validTagValue(name, color, category)) return false;
        for (auto& tag : tags) if (tag.id == id) {
            tag.name = name;
            tag.color = color;
            tag.category = category;
            return true;
        }
        return false;
    }

    bool removeTag(int id) {
        if (id == activeTagId) return false;
        for (const auto& session : sessions) if (session.tagId == id) return false;
        const auto it = std::find_if(tags.begin(), tags.end(),
                                     [id](const Tag& tag) { return tag.id == id; });
        if (it == tags.end()) return false;
        tags.erase(it);
        return true;
    }

    void start(int id, long long now) {
        if (!findTag(id) || !validTime(now) || id == activeTagId) return;
        long long boundary = now;
        for (const auto& session : sessions) boundary = (std::max)(boundary, session.end);
        if (activeTagId) boundary = (std::max)(boundary, activeSince);
        if (activeTagId && boundary > activeSince)
            sessions.push_back({activeTagId, activeSince, boundary});
        activeTagId = id;
        activeSince = boundary;
    }

    void stop(long long now) {
        if (!activeTagId || !validTime(now)) return;
        if (now > activeSince) sessions.push_back({activeTagId, activeSince, now});
        activeTagId = 0;
        activeSince = 0;
    }

    bool deletable(int id) const {
        if (!findTag(id) || id == activeTagId) return false;
        for (const auto& session : sessions) if (session.tagId == id) return false;
        return true;
    }

    bool needsRecovery(long long now, long long threshold = 120) const {
        return activeTagId != 0 && validTime(now) &&
               (lastSaved == 0 || now - lastSaved > threshold);
    }

    void resolveRecovery(Recovery choice) {
        if (!activeTagId || choice == Recovery::Continue) return;
        if (choice == Recovery::EndAtLastSaved && validTime(lastSaved) && lastSaved > activeSince)
            sessions.push_back({activeTagId, activeSince, lastSaved});
        activeTagId = 0;
        activeSince = 0;
    }

    DayStats stats(long long dayStart, long long dayEnd, long long now) const {
        DayStats result;
        const auto upper = (std::min)(dayEnd, now);
        if (dayStart >= upper) return result;
        const auto add = [&](int id, long long start, long long end) {
            const auto* tag = findTag(id);
            if (!tag || !validTime(start) || !validTime(end) || end <= start) return;
            const auto from = (std::max)(dayStart, start);
            const auto to = (std::min)(upper, end);
            if (to <= from) return;
            const auto duration = to - from;
            result.byTag[id] += duration;
            result.total += duration;
            switch (tag->category) {
                case Category::Work: result.work += duration; break;
                case Category::Play: result.play += duration; break;
                case Category::Other: result.other += duration; break;
            }
        };
        for (const auto& session : sessions) add(session.tagId, session.start, session.end);
        if (activeTagId) add(activeTagId, activeSince, (std::min)(now, maxTime));
        return result;
    }

    bool load(const std::wstring& path) {
        try {
            std::ifstream in(std::filesystem::path(path), std::ios::binary | std::ios::ate);
            if (!in || in.tellg() < 0 || in.tellg() > maxFileBytes) return false;
            in.seekg(0);
            std::array<char, 8> magic{};
            if (!in.read(magic.data(), magic.size())) return false;
            const bool current = magic == fileMagic;
            if (!current && magic != legacyMagic) return false;
            std::uint64_t count = 0, value = 0;
            Model candidate;
            candidate.tags.clear();
            if (!readNumber(in, count, 4) || count > maxTags) return false;
            candidate.tags.reserve(static_cast<std::size_t>(count));
            for (std::uint64_t i = 0; i < count; ++i) {
                Tag tag{};
                if (!readNumber(in, value, 4) || value == 0 || value > maxId) return false;
                tag.id = static_cast<int>(value);
                if (!readNumber(in, value, 4) || value > 0xFFFFFFu) return false;
                tag.color = static_cast<unsigned>(value);
                if (!readNumber(in, value, 4) || value > 2) return false;
                tag.category = static_cast<Category>(value);
                if (!readNumber(in, value, 4) || value == 0 || value > maxNameBytes) return false;
                std::string bytes(static_cast<std::size_t>(value), '\0');
                if (!in.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) ||
                    !decode(bytes, tag.name)) return false;
                candidate.tags.push_back(std::move(tag));
            }
            if (!readNumber(in, count, 4) || count > maxSessions) return false;
            candidate.sessions.reserve(static_cast<std::size_t>(count));
            for (std::uint64_t i = 0; i < count; ++i) {
                Session session{};
                if (!readNumber(in, value, 4) || value == 0 || value > maxId) return false;
                session.tagId = static_cast<int>(value);
                if (!readNumber(in, value, 8) || value > static_cast<std::uint64_t>(maxTime)) return false;
                session.start = static_cast<long long>(value);
                if (!readNumber(in, value, 8) || value > static_cast<std::uint64_t>(maxTime)) return false;
                session.end = static_cast<long long>(value);
                candidate.sessions.push_back(session);
            }
            if (!readNumber(in, value, 4) || value > maxId) return false;
            candidate.activeTagId = static_cast<int>(value);
            if (!readNumber(in, value, 8) || value > static_cast<std::uint64_t>(maxTime)) return false;
            candidate.activeSince = static_cast<long long>(value);
            candidate.lastSaved = 0;
            if (current) {
                if (!readNumber(in, value, 8) || value > static_cast<std::uint64_t>(maxTime)) return false;
                candidate.lastSaved = static_cast<long long>(value);
            }
            if (in.peek() != std::char_traits<char>::eof() || in.bad() || !candidate.validate()) return false;
            *this = std::move(candidate);
            return true;
        } catch (...) { return false; }
    }

    bool save(const std::wstring& path, long long savedAt = -1) const {
        std::filesystem::path temporary;
        try {
            Model candidate = *this;
            if (savedAt < 0) savedAt = static_cast<long long>(std::time(nullptr));
            if (candidate.activeTagId) savedAt = (std::max)(savedAt, candidate.activeSince);
            candidate.lastSaved = savedAt;
            if (!candidate.validate()) return false;
            const std::filesystem::path destination(path);
            if (destination.filename().empty()) return false;
            static std::atomic<unsigned long long> sequence{0};
            std::random_device random;
            temporary = destination;
            temporary += L".tmp-" + std::to_wstring(random()) + L"-" +
                         std::to_wstring(random()) + L"-" + std::to_wstring(++sequence);
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out.write(fileMagic.data(), fileMagic.size());
            writeNumber(out, candidate.tags.size(), 4);
            for (const auto& tag : candidate.tags) {
                std::string bytes;
                if (!encode(tag.name, bytes)) { out.close(); cleanup(temporary); return false; }
                writeNumber(out, tag.id, 4);
                writeNumber(out, tag.color, 4);
                writeNumber(out, static_cast<unsigned>(tag.category), 4);
                writeNumber(out, bytes.size(), 4);
                out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            }
            writeNumber(out, candidate.sessions.size(), 4);
            for (const auto& session : candidate.sessions) {
                writeNumber(out, session.tagId, 4);
                writeNumber(out, session.start, 8);
                writeNumber(out, session.end, 8);
            }
            writeNumber(out, candidate.activeTagId, 4);
            writeNumber(out, candidate.activeSince, 8);
            writeNumber(out, candidate.lastSaved, 8);
            out.flush();
            const bool written = static_cast<bool>(out);
            out.close();
            if (!written || !out) { cleanup(temporary); return false; }
#ifdef _WIN32
            if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                cleanup(temporary);
                return false;
            }
#else
            std::filesystem::rename(temporary, destination);
#endif
            return true;
        } catch (...) { cleanup(temporary); return false; }
    }

private:
    static constexpr std::size_t maxTags = 4096;
    static constexpr std::size_t maxSessions = 1000000;
    static constexpr std::size_t maxNameBytes = 4096;
    static constexpr long long maxFileBytes = 64 * 1024 * 1024;
    static constexpr long long maxTime = 253402300799LL;
    static constexpr std::uint64_t maxId = (std::numeric_limits<int>::max)();
    inline static constexpr std::array<char, 8> fileMagic{'T','R','A','C','K','E','R','2'};
    inline static constexpr std::array<char, 8> legacyMagic{'T','R','A','C','K','E','R','1'};

    static bool validTime(long long time) { return time >= 0 && time <= maxTime; }
    void insertSorted(const Session& session) {
        const auto at = std::upper_bound(sessions.begin(), sessions.end(), session,
            [](const Session& a, const Session& b) { return a.start < b.start; });
        sessions.insert(at, session);
    }
    static void cleanup(const std::filesystem::path& path) noexcept {
        std::error_code error;
        if (!path.empty()) std::filesystem::remove(path, error);
    }
    static bool encode(const std::wstring& name, std::string& bytes) {
        bytes.clear();
        if (name.empty() || name.size() > maxNameBytes) return false;
        for (std::size_t i = 0; i < name.size(); ++i) {
            std::uint32_t cp = static_cast<std::uint32_t>(name[i]);
            if constexpr (sizeof(wchar_t) == 2) {
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (++i == name.size()) return false;
                    const auto low = static_cast<std::uint32_t>(name[i]);
                    if (low < 0xDC00 || low > 0xDFFF) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + low - 0xDC00;
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) return false;
            }
            if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
            if (cp < 0x80) bytes.push_back(static_cast<char>(cp));
            else {
                const int length = cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
                bytes.push_back(static_cast<char>((length == 2 ? 0xC0 : length == 3 ? 0xE0 : 0xF0) |
                                                 (cp >> (6 * (length - 1)))));
                for (int part = length - 2; part >= 0; --part)
                    bytes.push_back(static_cast<char>(0x80 | ((cp >> (6 * part)) & 0x3F)));
            }
            if (bytes.size() > maxNameBytes) return false;
        }
        return true;
    }
    static bool decode(const std::string& bytes, std::wstring& name) {
        name.clear();
        for (std::size_t i = 0; i < bytes.size();) {
            const auto first = static_cast<unsigned char>(bytes[i++]);
            std::uint32_t cp = 0;
            unsigned length = 0;
            if (first < 0x80) { cp = first; length = 1; }
            else if (first >= 0xC2 && first <= 0xDF) { cp = first & 0x1F; length = 2; }
            else if (first >= 0xE0 && first <= 0xEF) { cp = first & 0x0F; length = 3; }
            else if (first >= 0xF0 && first <= 0xF4) { cp = first & 0x07; length = 4; }
            else return false;
            for (unsigned part = 1; part < length; ++part) {
                if (i == bytes.size()) return false;
                const auto next = static_cast<unsigned char>(bytes[i++]);
                if ((next & 0xC0) != 0x80) return false;
                cp = (cp << 6) | (next & 0x3F);
            }
            if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) ||
                (length == 2 && cp < 0x80) || (length == 3 && cp < 0x800) ||
                (length == 4 && cp < 0x10000)) return false;
            if constexpr (sizeof(wchar_t) == 2) {
                if (cp > 0xFFFF) {
                    cp -= 0x10000;
                    name.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
                    name.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
                } else name.push_back(static_cast<wchar_t>(cp));
            } else name.push_back(static_cast<wchar_t>(cp));
        }
        return !name.empty();
    }
    static bool validTagValue(const std::wstring& name, unsigned color, Category category) {
        std::string bytes;
        return color <= 0xFFFFFFu && static_cast<int>(category) >= 0 &&
               static_cast<int>(category) <= 2 && encode(name, bytes);
    }
    bool validate() {
        if (tags.size() > maxTags || sessions.size() > maxSessions) return false;
        std::unordered_set<int> ids;
        for (const auto& tag : tags)
            if (tag.id <= 0 || !ids.insert(tag.id).second ||
                !validTagValue(tag.name, tag.color, tag.category)) return false;
        std::sort(sessions.begin(), sessions.end(), [](const Session& a, const Session& b) {
            return a.start < b.start;
        });
        long long previousEnd = 0;
        for (const auto& session : sessions) {
            if (!ids.count(session.tagId) || !validTime(session.start) || !validTime(session.end) ||
                session.end <= session.start || session.start < previousEnd) return false;
            previousEnd = session.end;
        }
        if (!validTime(lastSaved)) return false;
        if (activeTagId == 0) return activeSince == 0;
        return ids.count(activeTagId) && validTime(activeSince) && activeSince >= previousEnd;
    }
    static bool readNumber(std::istream& in, std::uint64_t& value, unsigned size) {
        value = 0;
        for (unsigned i = 0; i < size; ++i) {
            const int byte = in.get();
            if (byte == std::char_traits<char>::eof()) return false;
            value |= static_cast<std::uint64_t>(static_cast<unsigned char>(byte)) << (8 * i);
        }
        return true;
    }
    static void writeNumber(std::ostream& out, std::uint64_t value, unsigned size) {
        for (unsigned i = 0; i < size; ++i) out.put(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
};

} // namespace tracker
