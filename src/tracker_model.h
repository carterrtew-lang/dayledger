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
constexpr unsigned CategoryFree = 1u;            // 畅玩电脑: the focus guard leaves this category alone
constexpr unsigned CategoryKnownFlags = CategoryFree;
struct Category { int id; std::wstring name; unsigned color; unsigned flags = 0; };
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

// ---- focus guard (专注守护): while a whitelist category is timed, which programs may be in front.
// A rule names a program (lowercase file name, or "*" for any) and how its windows are judged.
enum class GuardMode : unsigned {
    AllowAll = 0,       // every window of the program
    CheckTitle = 1,     // allowed unless the title has a distraction keyword (browsers)
    AllowOnly = 2,      // only windows whose title has one of the rule's keywords
};
struct GuardRule {
    std::wstring exe;
    GuardMode mode = GuardMode::AllowAll;
    std::vector<std::wstring> keywords;     // AllowOnly: 1..32
    std::vector<int> scope;                 // category ids; empty = every whitelist category
    bool operator==(const GuardRule& o) const {
        return exe == o.exe && mode == o.mode && keywords == o.keywords && scope == o.scope;
    }
};
constexpr unsigned GuardMinimize = 1u;      // "back to work" minimises the distracting window
constexpr unsigned GuardSound = 2u;         // a chime when the reminder appears
constexpr unsigned GuardLogTitles = 4u;     // the guard log keeps window titles
constexpr unsigned GuardKnownOptions = GuardMinimize | GuardSound | GuardLogTitles;
constexpr std::size_t kMaxGuardRules = 256, kMaxRuleKeywords = 32, kMaxGuardKeywords = 256,
                      kMaxGuardTextBytes = 256;
struct GuardConfig {
    bool enabled = false;
    unsigned graceSeconds = 3;      // 0..120: how long a violation may last before the reminder
    unsigned repeatSeconds = 60;    // 10..3600: a dismissed reminder comes back after this long
    unsigned releaseMinutes = 10;   // 1..240: how long a temporary release (临时放行) lasts
    unsigned idleSeconds = 120;     // 30..3600: no input for this long = away from the computer
    unsigned options = GuardSound | GuardLogTitles;
    std::vector<GuardRule> rules;
    std::vector<std::wstring> keywords;     // distraction keywords; only CheckTitle rules use them
    bool operator==(const GuardConfig& o) const {
        return enabled == o.enabled && graceSeconds == o.graceSeconds && repeatSeconds == o.repeatSeconds &&
               releaseMinutes == o.releaseMinutes && idleSeconds == o.idleSeconds && options == o.options &&
               rules == o.rules && keywords == o.keywords;
    }
    bool operator!=(const GuardConfig& o) const { return !(*this == o); }
};

// Office, WPS, PDF readers, development tools, terminals, Explorer and work chat apps are allowed;
// browsers are allowed unless the page title names a site on the distraction list.
inline std::vector<GuardRule> DefaultGuardRules() {
    static const wchar_t* const allowed[] = {
        L"winword.exe", L"excel.exe", L"powerpnt.exe", L"outlook.exe", L"olk.exe", L"onenote.exe",
        L"msaccess.exe", L"visio.exe", L"wps.exe", L"et.exe", L"wpp.exe", L"wpspdf.exe", L"acrobat.exe",
        L"acrord32.exe", L"sumatrapdf.exe", L"foxitpdfreader.exe", L"foxitphantom.exe", L"code.exe",
        L"cursor.exe", L"devenv.exe", L"idea64.exe", L"pycharm64.exe", L"clion64.exe", L"webstorm64.exe",
        L"rider64.exe", L"goland64.exe", L"studio64.exe", L"sublime_text.exe", L"notepad++.exe",
        L"windowsterminal.exe", L"openconsole.exe", L"cmd.exe", L"powershell.exe", L"pwsh.exe",
        L"mintty.exe", L"explorer.exe", L"notepad.exe", L"taskmgr.exe", L"calculatorapp.exe",
        L"snippingtool.exe", L"feishu.exe", L"lark.exe", L"dingtalk.exe", L"wxwork.exe", L"wemeetapp.exe",
        L"zoom.exe", L"ms-teams.exe", L"teams.exe", L"slack.exe", L"obsidian.exe", L"notion.exe",
        L"typora.exe", L"xmind.exe"};
    static const wchar_t* const browsers[] = {
        L"chrome.exe", L"msedge.exe", L"firefox.exe", L"brave.exe", L"opera.exe", L"vivaldi.exe",
        L"360se.exe", L"360chrome.exe", L"qqbrowser.exe", L"sogouexplorer.exe", L"2345explorer.exe"};
    std::vector<GuardRule> rules;
    for (const auto* exe : allowed) rules.push_back({exe, GuardMode::AllowAll, {}, {}});
    for (const auto* exe : browsers) rules.push_back({exe, GuardMode::CheckTitle, {}, {}});
    return rules;
}
inline std::vector<std::wstring> DefaultGuardKeywords() {
    return {L"\u77e5\u4e4e", L"\u54d4\u54e9\u54d4\u54e9", L"bilibili", L"\u5fae\u535a", L"\u6296\u97f3",
            L"\u5feb\u624b", L"\u5c0f\u7ea2\u4e66", L"\u8c46\u74e3", L"\u864e\u6251", L"\u8d34\u5427",
            L"\u6597\u9c7c", L"\u864e\u7259", L"\u7231\u5947\u827a", L"\u4f18\u9177",
            L"\u817e\u8baf\u89c6\u9891", L"\u8292\u679cTV", L"\u897f\u74dc\u89c6\u9891", L"youtube",
            L"netflix", L"twitch", L"reddit", L"\u6dd8\u5b9d", L"\u5929\u732b", L"\u62fc\u591a\u591a",
            L"\u95f2\u9c7c"};
}
inline GuardConfig DefaultGuardConfig() {
    GuardConfig config;
    config.rules = DefaultGuardRules();
    config.keywords = DefaultGuardKeywords();
    return config;
}
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

    GuardConfig guard = DefaultGuardConfig();

    // The default work / play / other categories (blue, orange, aqua) and one tag in each; play
    // (娱乐) is free time for the focus guard.
    Model() : categories{{0, L"\u5de5\u4f5c", 0x00D6782Au}, {1, L"\u5a31\u4e50", 0x003468EBu, CategoryFree},
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

    // Removes a category after moving its tags to `moveTo`; the last category cannot go. Guard
    // rules forget it; a rule that applied to it alone goes too.
    bool removeCategory(int id, int moveTo) {
        const int index = categoryIndex(id);
        if (index < 0 || categories.size() <= 1 || moveTo == id || !findCategory(moveTo)) return false;
        for (auto& tag : tags) if (tag.category == id) tag.category = moveTo;
        categories.erase(categories.begin() + index);
        auto& rules = guard.rules;
        for (auto it = rules.begin(); it != rules.end();) {
            auto& scope = it->scope;
            const bool scoped = !scope.empty();
            scope.erase(std::remove(scope.begin(), scope.end(), id), scope.end());
            if (scoped && scope.empty()) it = rules.erase(it);
            else ++it;
        }
        return true;
    }

    bool isFree(int categoryId) const {
        const auto* category = findCategory(categoryId);
        return category && (category->flags & CategoryFree);
    }
    bool setCategoryFree(int id, bool free) {
        for (auto& category : categories) if (category.id == id) {
            category.flags = free ? (category.flags | CategoryFree) : (category.flags & ~CategoryFree);
            return true;
        }
        return false;
    }

    // Replaces the guard settings when they are valid (texts, ranges, known categories).
    bool setGuard(const GuardConfig& config) {
        if (!validGuard(config)) return false;
        guard = config;
        return true;
    }
    static bool validGuardText(const std::wstring& text) {
        std::string bytes;
        if (!encode(text, bytes) || bytes.size() > kMaxGuardTextBytes) return false;
        for (wchar_t ch : text) if (ch < 0x20 || ch == 0x7F) return false;
        return true;
    }
    static bool validGuardExe(const std::wstring& exe) {
        if (exe == L"*") return true;
        if (!validGuardText(exe) || exe.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) return false;
        for (wchar_t ch : exe) if (ch >= L'A' && ch <= L'Z') return false;     // stored lowercase
        return exe.front() != L' ' && exe.back() != L' ';
    }
    bool validGuard(const GuardConfig& config) const {
        if (config.graceSeconds > 120 || config.repeatSeconds < 10 || config.repeatSeconds > 3600 ||
            config.releaseMinutes < 1 || config.releaseMinutes > 240 || config.idleSeconds < 30 ||
            config.idleSeconds > 3600 || (config.options & ~GuardKnownOptions) ||
            config.rules.size() > kMaxGuardRules || config.keywords.size() > kMaxGuardKeywords) return false;
        for (const auto& keyword : config.keywords) if (!validGuardText(keyword)) return false;
        for (const auto& rule : config.rules) {
            if (!validGuardExe(rule.exe) || static_cast<unsigned>(rule.mode) > 2 ||
                rule.keywords.size() > kMaxRuleKeywords || rule.scope.size() > kMaxCategories) return false;
            if (rule.mode == GuardMode::AllowOnly ? rule.keywords.empty() : !rule.keywords.empty()) return false;
            for (const auto& keyword : rule.keywords) if (!validGuardText(keyword)) return false;
            std::unordered_set<int> seen;
            for (int id : rule.scope) if (!findCategory(id) || !seen.insert(id).second) return false;
        }
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

    // The same numbers as stats() for each of the consecutive ranges [bounds[i], bounds[i + 1])
    // (bounds ascending), in one pass over the ledger: a record crossing a boundary is split.
    std::vector<DayStats> statsBetween(const std::vector<long long>& bounds, long long now) const {
        std::vector<DayStats> result(bounds.size() > 1 ? bounds.size() - 1 : 0);
        if (result.empty()) return result;
        const auto add = [&](int id, long long start, long long end) {
            const auto* tag = findTag(id);
            if (!tag || !validTime(start) || !validTime(end) || end <= start) return;
            end = (std::min)(end, now);
            const auto first = std::upper_bound(bounds.begin(), bounds.end(), start);
            std::size_t i = first == bounds.begin() ? 0 : static_cast<std::size_t>(first - bounds.begin()) - 1;
            for (; i < result.size() && bounds[i] < end; ++i) {
                const auto from = (std::max)(bounds[i], start), to = (std::min)(bounds[i + 1], end);
                if (to <= from) continue;
                auto& range = result[i];
                range.byTag[id] += to - from;
                range.byCategory[tag->category] += to - from;
                range.total += to - from;
            }
        };
        for (const auto& session : sessions) {
            add(session.tagId, session.start, session.end);
            if (!(session.flags & SessionPomodoro) || session.end > now || !findTag(session.tagId)) continue;
            const auto after = std::lower_bound(bounds.begin(), bounds.end(), session.end);   // ended in (b[i], b[i+1]]
            if (after == bounds.begin() || after == bounds.end()) continue;
            auto& range = result[static_cast<std::size_t>(after - bounds.begin()) - 1];
            ++range.pomodoros;
            ++range.pomodorosByTag[session.tagId];
        }
        if (activeTagId) add(activeTagId, activeSince, (std::min)(now, maxTime));
        return result;
    }

    // Seconds per hour of the day, by category, over the days [dayBounds[i], dayBounds[i + 1]).
    // A day that is not 24 hours long (a daylight-saving change) is spread evenly over its hours.
    std::map<int, std::array<long long, 24>> hoursByCategory(const std::vector<long long>& dayBounds,
                                                             long long now) const {
        std::map<int, std::array<long long, 24>> result;
        if (dayBounds.size() < 2) return result;
        const auto add = [&](int id, long long start, long long end) {
            const auto* tag = findTag(id);
            if (!tag || !validTime(start) || !validTime(end) || end <= start) return;
            end = (std::min)(end, now);
            const auto first = std::upper_bound(dayBounds.begin(), dayBounds.end(), start);
            std::size_t i = first == dayBounds.begin() ? 0 : static_cast<std::size_t>(first - dayBounds.begin()) - 1;
            for (; i + 1 < dayBounds.size() && dayBounds[i] < end; ++i) {
                const long long day = dayBounds[i], length = dayBounds[i + 1] - day;
                if (length <= 0) continue;
                auto& hours = result[tag->category];
                for (long long t = (std::max)(day, start), to = (std::min)(day + length, end); t < to;) {
                    const long long hour = (std::min)(23LL, (t - day) * 24 / length);
                    const long long hourEnd = day + (hour + 1) * length / 24;
                    const long long stop = (std::min)(to, (std::max)(hourEnd, t + 1));
                    hours[static_cast<std::size_t>(hour)] += stop - t;
                    t = stop;
                }
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
            // TRACKER5 adds category flags and the focus guard settings; TRACKER4 the category table;
            // TRACKER3 per-session flags; TRACKER2 the checkpoint.
            int version = 0;
            if (magic == fileMagic) version = 5;
            else if (magic == version4Magic) version = 4;
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
                    if (version >= 5) {
                        if (!readNumber(in, value, 4) || (value & ~std::uint64_t(CategoryKnownFlags))) return false;
                        category.flags = static_cast<unsigned>(value);
                    }
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
            if (version >= 5) {
                if (!readGuard(in, candidate.guard)) return false;
            } else {
                // Written before the focus guard: play (id 1) is free time, the guard starts switched off.
                for (auto& category : candidate.categories) category.flags = category.id == 1 ? CategoryFree : 0u;
                candidate.guard = DefaultGuardConfig();
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
                writeNumber(out, category.flags, 4);
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
            if (!writeGuard(out, candidate.guard)) { out.close(); cleanup(temporary); return false; }
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
    inline static constexpr std::array<char, 8> fileMagic{'T','R','A','C','K','E','R','5'};
    inline static constexpr std::array<char, 8> version4Magic{'T','R','A','C','K','E','R','4'};
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
                !validNameColor(category.name, category.color) || (category.flags & ~CategoryKnownFlags)) return false;
        if (!validGuard(guard)) return false;
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
    static bool readText(std::istream& in, std::wstring& text) {
        std::uint64_t length = 0;
        if (!readNumber(in, length, 4) || length == 0 || length > kMaxGuardTextBytes) return false;
        std::string bytes(static_cast<std::size_t>(length), '\0');
        return in.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) && decode(bytes, text);
    }
    static bool writeText(std::ostream& out, const std::wstring& text) {
        std::string bytes;
        if (!encode(text, bytes) || bytes.size() > kMaxGuardTextBytes) return false;
        writeNumber(out, bytes.size(), 4);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return true;
    }
    // The guard block of TRACKER5: settings, rules, distraction keywords. Values are range-checked
    // by validGuard() once the whole file is read.
    static bool readGuard(std::istream& in, GuardConfig& config) {
        std::uint64_t value = 0, count = 0;
        unsigned* const numbers[] = {&config.graceSeconds, &config.repeatSeconds, &config.releaseMinutes,
                                     &config.idleSeconds, &config.options};
        if (!readNumber(in, value, 4) || value > 1) return false;
        config.enabled = value == 1;
        for (unsigned* number : numbers) {
            if (!readNumber(in, value, 4) || value > 0xFFFFFFFFu) return false;
            *number = static_cast<unsigned>(value);
        }
        if (!readNumber(in, count, 4) || count > kMaxGuardRules) return false;
        config.rules.clear();
        for (std::uint64_t i = 0; i < count; ++i) {
            GuardRule rule;
            std::uint64_t items = 0;
            if (!readText(in, rule.exe) || !readNumber(in, value, 4) || value > 2) return false;
            rule.mode = static_cast<GuardMode>(value);
            if (!readNumber(in, items, 4) || items > kMaxRuleKeywords) return false;
            for (std::uint64_t k = 0; k < items; ++k) {
                std::wstring keyword;
                if (!readText(in, keyword)) return false;
                rule.keywords.push_back(std::move(keyword));
            }
            if (!readNumber(in, items, 4) || items > kMaxCategories) return false;
            for (std::uint64_t k = 0; k < items; ++k) {
                if (!readNumber(in, value, 4) || value > maxId) return false;
                rule.scope.push_back(static_cast<int>(value));
            }
            config.rules.push_back(std::move(rule));
        }
        if (!readNumber(in, count, 4) || count > kMaxGuardKeywords) return false;
        config.keywords.clear();
        for (std::uint64_t i = 0; i < count; ++i) {
            std::wstring keyword;
            if (!readText(in, keyword)) return false;
            config.keywords.push_back(std::move(keyword));
        }
        return true;
    }
    static bool writeGuard(std::ostream& out, const GuardConfig& config) {
        writeNumber(out, config.enabled ? 1 : 0, 4);
        for (unsigned number : {config.graceSeconds, config.repeatSeconds, config.releaseMinutes,
                                config.idleSeconds, config.options}) writeNumber(out, number, 4);
        writeNumber(out, config.rules.size(), 4);
        for (const auto& rule : config.rules) {
            if (!writeText(out, rule.exe)) return false;
            writeNumber(out, static_cast<unsigned>(rule.mode), 4);
            writeNumber(out, rule.keywords.size(), 4);
            for (const auto& keyword : rule.keywords) if (!writeText(out, keyword)) return false;
            writeNumber(out, rule.scope.size(), 4);
            for (int id : rule.scope) writeNumber(out, static_cast<std::uint64_t>(id), 4);
        }
        writeNumber(out, config.keywords.size(), 4);
        for (const auto& keyword : config.keywords) if (!writeText(out, keyword)) return false;
        return true;
    }
};

} // namespace tracker
