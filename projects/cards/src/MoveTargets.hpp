#pragma once
#include "Settings.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace cards::moves {

constexpr int kNintendoPage = 2;

struct Target {
    int page;
    bool hidden;
};

namespace detail {
inline bool isCustom(const Settings& s, int page) {
    return page >= Settings::kBuiltinPages && page < Settings::kBuiltinPages + static_cast<int>(s.customPages.size());
}
inline void erase(std::vector<std::string>& v, const std::string& id) {
    v.erase(std::remove(v.begin(), v.end(), id), v.end());
}
inline void addOnce(std::vector<std::string>& v, const std::string& id) {
    if (std::find(v.begin(), v.end(), id) == v.end()) v.push_back(id);
}
} // namespace detail

// The pages a card can be moved to from `current`, in the page order. Library
// pages (Games, Homebrew, Media) only when `libraryMovable`; custom pages that
// aren't genre pages always. Never Nintendo, the current page, or a page that
// doesn't exist.
inline std::vector<Target> destinations(const Settings& s, int current, bool libraryMovable) {
    std::vector<Target> out;
    for (int page : s.pageOrder) {
        if (page == current || page == kNintendoPage) continue;
        bool ok = false;
        if (page >= 0 && page < Settings::kBuiltinPages) ok = libraryMovable;
        else if (detail::isCustom(s, page)) ok = !s.customPages[page - Settings::kBuiltinPages].genre;
        if (!ok) continue;
        const bool hidden = std::find(s.hiddenPages.begin(), s.hiddenPages.end(), page) != s.hiddenPages.end();
        out.push_back({page, hidden});
    }
    return out;
}

inline bool isDestination(const Settings& s, int current, bool libraryMovable, int page) {
    const auto list = destinations(s, current, libraryMovable);
    return std::any_of(list.begin(), list.end(), [page](const Target& t) { return t.page == page; });
}

// The custom pages' side of moving card `id` from page `from` to page `to`
// (page ids; custom index = page - kBuiltinPages; others ignored).
inline void applyCustomMove(std::vector<CustomPage>& pages, const std::string& id, int from, int to) {
    const int count = static_cast<int>(pages.size());
    auto custom = [count](int page) { return page >= Settings::kBuiltinPages && page < Settings::kBuiltinPages + count; };
    auto at = [&pages](int page) -> CustomPage& { return pages[page - Settings::kBuiltinPages]; };
    if (custom(from)) {
        detail::erase(at(from).titles, id);
        detail::erase(at(from).moved, id);
    }
    for (int i = 0; i < count; ++i) {
        auto& page = pages[i];
        if (std::find(page.moved.begin(), page.moved.end(), id) == page.moved.end()) continue;
        detail::erase(page.titles, id);
        detail::erase(page.moved, id);
    }
    if (custom(to)) {
        detail::addOnce(at(to).titles, id);
        detail::addOnce(at(to).moved, id);
    }
}

} // namespace cards::moves
