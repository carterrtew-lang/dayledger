#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
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

// Categories (大类) are user-editable; tags (小类) belong to one. Colours are COLORREF (0x00BBGGRR).
// Ids 0, 1 and 2 are the default work / play / other categories, which is also how files written
// before categories became editable (TRACKER1-3) referred to them.
struct Category { int id; std::wstring name; unsigned color; };
struct Tag { int id; std::wstring name; unsigned color; int category; };
// Session flags. A completed pomodoro is the record that ran until its focus countdown ended.
constexpr unsigned SessionPomodoro = 1u;
constexpr unsigned SessionKnownFlags = SessionPomodoro;
struct Session { int tagId; long long start; long long end; unsigned flags = 0; };
struct DayStats {
    long long total = 0;
    std::map<int, long long> byTag, byCategory;
    int pomodoros = 0;                  // completed pomodoros that ended inside the range
    std::map<int, int> pomodorosByTag;
    long long ofCategory(int id) const {
        const auto found = byCategory.find(id);
        return found == byCategory.end() ? 0 : found->second;
    }
    long long ofTag(int id) const {
        const auto found = byTag.find(id);
        return found == byTag.end() ? 0 : found->second;
    }
};

// ---- colour families: shades of one hue, so a category's tags read as one group.
// Computed in OKLCH (perceptual lightness, chroma, hue); chroma is reduced to stay inside sRGB.

struct Oklch { double L, C, h; };

inline double SrgbToLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
inline double LinearToSrgb(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - 0.055; }

inline Oklch ToOklch(unsigned color) {
    const double r = SrgbToLinear((color & 0xFF) / 255.0);
    const double g = SrgbToLinear(((color >> 8) & 0xFF) / 255.0);
    const double b = SrgbToLinear(((color >> 16) & 0xFF) / 255.0);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    const double L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    const double A = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    const double B = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    return {L, std::hypot(A, B), std::atan2(B, A)};
}

// Converts back to a COLORREF; false when the colour lies outside sRGB.
inline bool FromOklch(const Oklch& c, unsigned& out) {
    const double A = c.C * std::cos(c.h), B = c.C * std::sin(c.h);
    const double l1 = c.L + 0.3963377774 * A + 0.2158037573 * B;
    const double m1 = c.L - 0.1055613458 * A - 0.0638541728 * B;
    const double s1 = c.L - 0.0894841775 * A - 1.2914855480 * B;
    const double l = l1 * l1 * l1, m = m1 * m1 * m1, s = s1 * s1 * s1;
    const double rgb[3] = {4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                           -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                           -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s};
    out = 0;
    for (int i = 0; i < 3; ++i) {
        if (rgb[i] < -1e-4 || rgb[i] > 1 + 1e-4) return false;
        const double v = LinearToSrgb((std::min)(1.0, (std::max)(0.0, rgb[i])));
        out |= static_cast<unsigned>(std::lround((std::min)(1.0, (std::max)(0.0, v)) * 255)) << (8 * i);
    }
    return true;
}

// The base hue at lightness L, with as much of the base chroma as sRGB allows.
inline unsigned ShadeAt(const Oklch& base, double L) {
    unsigned out = 0;
    for (double C = base.C; C > 1e-4; C *= 0.95)
        if (FromOklch({L, C, base.h}, out)) return out;
    FromOklch({L, 0.0, base.h}, out);
    return out;
}

constexpr int kFamilySize = 8;
constexpr double kFamilyDarkest = 0.40, kFamilyLightest = 0.83;

// Eight shades of a colour from deep to light; the shade nearest the base is the base itself.
inline std::vector<unsigned> ColorFamily(unsigned base) {
    const Oklch b = ToOklch(base & 0xFFFFFFu);
    std::vector<unsigned> shades;
    int closest = 0;
    for (int i = 0; i < kFamilySize; ++i) {
        const double L = kFamilyDarkest + (kFamilyLightest - kFamilyDarkest) * i / (kFamilySize - 1);
        shades.push_back(ShadeAt(b, L));
        const double nearest = kFamilyDarkest + (kFamilyLightest - kFamilyDarkest) * closest / (kFamilySize - 1);
        if (std::abs(L - b.L) < std::abs(nearest - b.L)) closest = i;
    }
    shades[static_cast<std::size_t>(closest)] = base & 0xFFFFFFu;
    return shades;
}

inline double ColorDistance(unsigned a, unsigned b) {
    const Oklch x = ToOklch(a), y = ToOklch(b);
    const double ax = x.C * std::cos(x.h), bx = x.C * std::sin(x.h);
    const double ay = y.C * std::cos(y.h), by = y.C * std::sin(y.h);
    return std::sqrt((x.L - y.L) * (x.L - y.L) + (ax - ay) * (ax - ay) + (bx - by) * (bx - by));
}

// `count` shades of one family spread evenly in lightness; the one nearest the base is the base.
inline std::vector<unsigned> SpreadShades(unsigned base, std::size_t count) {
    if (count <= 1) return std::vector<unsigned>(count, base & 0xFFFFFFu);
    const Oklch b = ToOklch(base & 0xFFFFFFu);
    const double low = 0.44, high = 0.80;
    std::vector<unsigned> shades;
    std::size_t closest = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const double L = low + (high - low) * static_cast<double>(i) / static_cast<double>(count - 1);
        shades.push_back(ShadeAt(b, L));
        const double nearest = low + (high - low) * static_cast<double>(closest) / static_cast<double>(count - 1);
        if (std::abs(L - b.L) < std::abs(nearest - b.L)) closest = i;
    }
    shades[closest] = base & 0xFFFFFFu;
    return shades;
}

enum class Recovery { EndAtLastSaved, Continue, Discard };

// Outcome of a record edit. `conflict` names the overlapping record (an index into
// Model::sessions), or -2 for the running segment, when status is Overlap.
enum class EditStatus { Ok, UnknownTag, InvalidRange, InFuture, Overlap, NotFound, NotRunning };

constexpr std::size_t kMaxCategories = 64;
struct EditResult {
    EditStatus status = EditStatus::Ok;
    int conflict = -1;
    explicit operator bool() const { return status == EditStatus::Ok; }
};

class Model {
public:
    std::vector<Category> categories;
    std::vector<Tag> tags;
    std::vector<Session> sessions;   // kept sorted by start, never overlapping
    int activeTagId = 0;
    long long activeSince = 0;
    long long lastSaved = 0;

    // The default work / play / other categories (blue, orange, aqua) and one tag in each.
    Model() : categories{{0, L"\u5de5\u4f5c", 0x00D6782Au}, {1, L"\u5a31\u4e50", 0x003468EBu},
                         {2, L"\u5176\u4ed6", 0x007AAF1Bu}},
              tags{{1, L"\u5de5\u4f5c", 0x00D6782Au, 0}, {2, L"\u5a31\u4e50", 0x003468EBu, 1},
                   {3, L"\u5176\u4ed6", 0x007AAF1Bu, 2}} {}

    // ---- categories

    const Category* findCategory(int id) const {
        for (const auto& category : categories) if (category.id == id) return &category;
        return nullptr;
    }

    int categoryIndex(int id) const {
        for (std::size_t i = 0; i < categories.size(); ++i)
            if (categories[i].id == id) return static_cast<int>(i);
        return -1;
    }

    std::size_t tagsIn(int categoryId) const {
        return static_cast<std::size_t>(std::count_if(tags.begin(), tags.end(),
            [categoryId](const Tag& tag) { return tag.category == categoryId; }));
    }

    // Returns the new category's id, or -1.
    int addCategory(const std::wstring& name, unsigned color) {
        if (categories.size() >= kMaxCategories || !validNameColor(name, color)) return -1;
        int id = -1;
        for (const auto& category : categories) id = (std::max)(id, category.id);
        if (id >= static_cast<int>(maxId)) return -1;
        categories.push_back({++id, name, color});
        return id;
    }

    bool updateCategory(int id, const std::wstring& name, unsigned color) {
        if (!validNameColor(name, color)) return false;
        for (auto& category : categories) if (category.id == id) {
            category.name = name;
            category.color = color;
            return true;
        }
        return false;
    }

    // Removes a category after moving its tags to `moveTo`; the last category cannot go.
    bool removeCategory(int id, int moveTo) {
        const int index = categoryIndex(id);
        if (index < 0 || categories.size() <= 1 || moveTo == id || !findCategory(moveTo)) return false;
        for (auto& tag : tags) if (tag.category == id) tag.category = moveTo;
        categories.erase(categories.begin() + index);
        return true;
    }

    bool moveCategory(int id, int delta) {
        const int from = categoryIndex(id);
        const int to = from + (delta < 0 ? -1 : 1);
        if (from < 0 || delta == 0 || to < 0 || to >= static_cast<int>(categories.size())) return false;
        std::swap(categories[static_cast<std::size_t>(from)], categories[static_cast<std::size_t>(to)]);
        return true;
    }

    // A colour for a new tag in the category: the first shade of its family that no tag there uses,
    // starting at the category colour and moving outwards.
    unsigned nextTagColor(int categoryId) const {
        const auto* category = findCategory(categoryId);
        if (!category) return 0x00808080u;
        const auto family = ColorFamily(category->color);
        int base = 0;
        for (int i = 0; i < kFamilySize; ++i) if (family[static_cast<std::size_t>(i)] == (category->color & 0xFFFFFFu)) base = i;
        for (int step = 0; step < 2 * kFamilySize; ++step) {
            const int index = base + (step % 2 ? (step + 1) / 2 : -(step / 2));
            if (index < 0 || index >= kFamilySize) continue;
            const unsigned shade = family[static_cast<std::size_t>(index)];
            const bool used = std::any_of(tags.begin(), tags.end(), [&](const Tag& tag) {
                return tag.category == categoryId && ColorDistance(tag.color, shade) < 0.02;
            });
            if (!used) return shade;
        }
        return category->color & 0xFFFFFFu;
    }

    // Gives every tag of the category a distinct shade of the category colour (first tag: the colour).
    void recolorCategory(int categoryId) {
        const auto* category = findCategory(categoryId);
        if (!category) return;
        std::vector<Tag*> members;
        for (auto& tag : tags) if (tag.category == categoryId) members.push_back(&tag);
        auto shades = SpreadShades(category->color, members.size());
        const auto base = std::find(shades.begin(), shades.end(), category->color & 0xFFFFFFu);
        if (base != shades.end()) std::rotate(shades.begin(), base, base + 1);   // the base colour first
        for (std::size_t i = 0; i < members.size(); ++i) members[i]->color = shades[i];
    }

    // ---- tags

    const Tag* findTag(int id) const {
        for (const auto& tag : tags) if (tag.id == id) return &tag;
        return nullptr;
    }

    int tagIndex(int id) const {
        for (std::size_t i = 0; i < tags.size(); ++i) if (tags[i].id == id) return static_cast<int>(i);
        return -1;
    }

    // Moves a tag one place up (delta < 0) or down (delta > 0) among the tags of its category.
    bool moveTag(int id, int delta) {
        const int from = tagIndex(id);
        if (from < 0 || delta == 0) return false;
        const int category = tags[static_cast<std::size_t>(from)].category;
        int to = from;
        do to += delta < 0 ? -1 : 1;
        while (to >= 0 && to < static_cast<int>(tags.size()) && tags[static_cast<std::size_t>(to)].category != category);
        if (to < 0 || to >= static_cast<int>(tags.size())) return false;
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

    EditResult addSession(int tagId, long long start, long long end, long long now,
                          unsigned flags = 0) {
        const auto result = checkRange(tagId, start, end, now);
        if (!result) return result;
        if (sessions.size() >= maxSessions) return {EditStatus::InvalidRange};
        insertSorted({tagId, start, end, flags & SessionKnownFlags});
        return {};
    }

    // Edits keep the record's flags: a retagged or re-timed pomodoro still counts.
    EditResult updateSession(std::size_t index, int tagId, long long start, long long end,
                             long long now) {
        if (index >= sessions.size()) return {EditStatus::NotFound};
        const auto result = checkRange(tagId, start, end, now, static_cast<long long>(index));
        if (!result) return result;
        const unsigned flags = sessions[index].flags;
        sessions.erase(sessions.begin() + static_cast<std::ptrdiff_t>(index));
        insertSorted({tagId, start, end, flags});
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

    int addTag(const std::wstring& name, unsigned color, int category) {
        if (tags.size() >= maxTags || !validTagValue(name, color, category)) return 0;
        int id = 0;
        for (const auto& tag : tags) id = (std::max)(id, tag.id);
        if (id == (std::numeric_limits<int>::max)()) return 0;
        tags.push_back({++id, name, color, category});
        return id;
    }

    bool updateTag(int id, const std::wstring& name, unsigned color, int category) {
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

    // Ends the running segment at `now`; `flags` marks the record (e.g. a completed pomodoro).
    void stop(long long now, unsigned flags = 0) {
        if (!activeTagId || !validTime(now)) return;
        if (now > activeSince) sessions.push_back({activeTagId, activeSince, now, flags & SessionKnownFlags});
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
            result.byCategory[tag->category] += duration;
            result.total += duration;
        };
        for (const auto& session : sessions) {
            add(session.tagId, session.start, session.end);
            if ((session.flags & SessionPomodoro) && session.end > dayStart && session.end <= upper &&
                findTag(session.tagId)) {
                ++result.pomodoros;
                ++result.pomodorosByTag[session.tagId];
            }
        }
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
            // TRACKER4 adds the category table; TRACKER3 per-session flags; TRACKER2 the checkpoint.
            int version = 0;
            if (magic == fileMagic) version = 4;
            else if (magic == version3Magic) version = 3;
            else if (magic == version2Magic) version = 2;
            else if (magic == legacyMagic) version = 1;
            else return false;
            std::uint64_t count = 0, value = 0;
            Model candidate;
            candidate.tags.clear();
            if (version >= 4) {
                candidate.categories.clear();
                if (!readNumber(in, count, 4) || count == 0 || count > kMaxCategories) return false;
                for (std::uint64_t i = 0; i < count; ++i) {
                    Category category{};
                    if (!readNumber(in, value, 4) || value > maxId) return false;
                    category.id = static_cast<int>(value);
                    if (!readNumber(in, value, 4) || value > 0xFFFFFFu) return false;
                    category.color = static_cast<unsigned>(value);
                    if (!readNumber(in, value, 4) || value == 0 || value > maxNameBytes) return false;
                    std::string bytes(static_cast<std::size_t>(value), '\0');
                    if (!in.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) ||
                        !decode(bytes, category.name)) return false;
                    candidate.categories.push_back(std::move(category));
                }
            }
            if (!readNumber(in, count, 4) || count > maxTags) return false;
            candidate.tags.reserve(static_cast<std::size_t>(count));
            for (std::uint64_t i = 0; i < count; ++i) {
                Tag tag{};
                if (!readNumber(in, value, 4) || value == 0 || value > maxId) return false;
                tag.id = static_cast<int>(value);
                if (!readNumber(in, value, 4) || value > 0xFFFFFFu) return false;
                tag.color = static_cast<unsigned>(value);
                if (!readNumber(in, value, 4) || value > (version >= 4 ? maxId : 2)) return false;
                tag.category = static_cast<int>(value);
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
                if (version >= 3) {
                    if (!readNumber(in, value, 4) || (value & ~std::uint64_t(SessionKnownFlags))) return false;
                    session.flags = static_cast<unsigned>(value);
                }
                candidate.sessions.push_back(session);
            }
            if (!readNumber(in, value, 4) || value > maxId) return false;
            candidate.activeTagId = static_cast<int>(value);
            if (!readNumber(in, value, 8) || value > static_cast<std::uint64_t>(maxTime)) return false;
            candidate.activeSince = static_cast<long long>(value);
            candidate.lastSaved = 0;
            if (version >= 2) {
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
            writeNumber(out, candidate.categories.size(), 4);
            for (const auto& category : candidate.categories) {
                std::string bytes;
                if (!encode(category.name, bytes)) { out.close(); cleanup(temporary); return false; }
                writeNumber(out, static_cast<std::uint64_t>(category.id), 4);
                writeNumber(out, category.color, 4);
                writeNumber(out, bytes.size(), 4);
                out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            }
            writeNumber(out, candidate.tags.size(), 4);
            for (const auto& tag : candidate.tags) {
                std::string bytes;
                if (!encode(tag.name, bytes)) { out.close(); cleanup(temporary); return false; }
                writeNumber(out, tag.id, 4);
                writeNumber(out, tag.color, 4);
                writeNumber(out, static_cast<std::uint64_t>(tag.category), 4);
                writeNumber(out, bytes.size(), 4);
                out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            }
            writeNumber(out, candidate.sessions.size(), 4);
            for (const auto& session : candidate.sessions) {
                writeNumber(out, session.tagId, 4);
                writeNumber(out, session.start, 8);
                writeNumber(out, session.end, 8);
                writeNumber(out, session.flags, 4);
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
    inline static constexpr std::array<char, 8> fileMagic{'T','R','A','C','K','E','R','4'};
    inline static constexpr std::array<char, 8> version3Magic{'T','R','A','C','K','E','R','3'};
    inline static constexpr std::array<char, 8> version2Magic{'T','R','A','C','K','E','R','2'};
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
    static bool validNameColor(const std::wstring& name, unsigned color) {
        std::string bytes;
        return color <= 0xFFFFFFu && encode(name, bytes);
    }
    bool validTagValue(const std::wstring& name, unsigned color, int category) const {
        return validNameColor(name, color) && findCategory(category) != nullptr;
    }
    bool validate() {
        if (tags.size() > maxTags || sessions.size() > maxSessions) return false;
        if (categories.empty() || categories.size() > kMaxCategories) return false;
        std::unordered_set<int> categoryIds;
        for (const auto& category : categories)
            if (category.id < 0 || !categoryIds.insert(category.id).second ||
                !validNameColor(category.name, category.color)) return false;
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
                session.end <= session.start || session.start < previousEnd ||
                (session.flags & ~SessionKnownFlags)) return false;
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
