#include "MoveTargets.hpp"
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

namespace cards {
void writeLater(const std::string&, std::string) {}
}

using cards::CustomPage;
using cards::Settings;
namespace mv = cards::moves;

static std::vector<int> pagesOf(const std::vector<mv::Target>& t) {
    std::vector<int> out;
    for (const auto& x : t) out.push_back(x.page);
    return out;
}
static bool has(const std::vector<std::string>& v, const std::string& id) { return std::count(v.begin(), v.end(), id) > 0; }
static int count(const std::vector<std::string>& v, const std::string& id) { return static_cast<int>(std::count(v.begin(), v.end(), id)); }

int main() {
    Settings s;
    s.customPages = {{"Faves", {}, {}, false}, {"RPG", {}, {}, true}, {"Kids", {}, {}, false}};
    s.pageOrder = {3, 0, 4, 1, 2, 5, 6};
    s.hiddenPages = {6};

    auto d = mv::destinations(s, 0, true);
    assert((pagesOf(d) == std::vector<int>{3, 4, 1, 6}));
    for (const auto& t : d) assert(t.hidden == (t.page == 6));
    assert((pagesOf(mv::destinations(s, 4, true)) == std::vector<int>{3, 0, 1, 6}));
    assert((pagesOf(mv::destinations(s, 2, false)) == std::vector<int>{4, 6}));

    assert(mv::isDestination(s, 0, true, 4));
    assert(!mv::isDestination(s, 0, true, 0));
    assert(!mv::isDestination(s, 0, true, 5));
    assert(!mv::isDestination(s, 0, true, 7));

    const std::string id = "0100";
    {
        std::vector<CustomPage> p = s.customPages;
        mv::applyCustomMove(p, id, 0, 4);
        mv::applyCustomMove(p, id, 0, 4);
        assert(count(p[0].titles, id) == 1 && count(p[0].moved, id) == 1);
        mv::applyCustomMove(p, id, 4, 0);
        assert(!has(p[0].titles, id) && !has(p[0].moved, id));
    }
    {
        std::vector<CustomPage> p = s.customPages;
        p[2].titles = {id};
        p[0].titles = p[0].moved = {id};
        mv::applyCustomMove(p, id, 0, 6);
        assert(!has(p[0].titles, id) && !has(p[0].moved, id));
        assert(count(p[2].titles, id) == 1 && count(p[2].moved, id) == 1);
    }
    {
        std::vector<CustomPage> p = s.customPages;
        p[2].titles = {id};
        mv::applyCustomMove(p, id, 0, 4);
        assert(count(p[2].titles, id) == 1 && !has(p[2].moved, id));
        assert(count(p[0].titles, id) == 1 && count(p[0].moved, id) == 1);
    }
    {
        std::vector<CustomPage> p = s.customPages;
        p[1].titles = {"x"};
        const auto before = p;
        mv::applyCustomMove(p, id, 0, 99);
        mv::applyCustomMove(p, id, 99, 1);  // to a built-in page 1: only removes
        mv::applyCustomMove(p, id, -3, 2);
        mv::applyCustomMove(p, id, 50, 60);
        for (size_t i = 0; i < p.size(); ++i)
            assert(p[i].titles == before[i].titles && p[i].moved == before[i].moved);
    }
    return 0;
}
