#include "PlusMenu.hpp"

#include <algorithm>
#include <cassert>
#include <vector>

using namespace cards::plus;
using V = std::vector<Entry>;

static int count(const V& v, Entry e) { return (int)std::count(v.begin(), v.end(), e); }

int main()
{
    {
        Card c; c.game = true; c.deletable = true;
        assert((entries(Level::Top, c) == V{Entry::Play, Entry::Details, Entry::Favourite, Entry::Rearrange, Entry::MoveTo,
                                            Entry::Appearance, Entry::Info, Entry::More, Entry::Delete}));
        assert((entries(Level::Appearance, c) == V{Entry::Rename, Entry::CardPicture, Entry::BackgroundPicture,
                                                   Entry::TitlePicture, Entry::IconPicture, Entry::DownloadPictures}));
        assert((entries(Level::Info, c) == V{Entry::AboutGame, Entry::Note, Entry::CopyMusic}));
        assert((entries(Level::More, c) == V{Entry::SearchGames, Entry::Hide}));
        assert(entries(Level::MoveTo, c).empty());
        c.hasUpdate = true; c.canClose = true;
        V t = entries(Level::Top, c);
        assert((V(t.begin(), t.begin() + 5) == V{Entry::Play, Entry::Update, Entry::CloseSoftware, Entry::Details, Entry::Favourite}));
        assert(t.size() == 11);
    }
    {
        Card c; c.homebrew = true; c.deletable = true;
        assert((entries(Level::Appearance, c) == V{Entry::Rename, Entry::CardPicture, Entry::BackgroundPicture,
                                                   Entry::TitlePicture, Entry::IconPicture, Entry::FullCard,
                                                   Entry::DownloadPictures}));
        assert(entries(Level::Info, c) == V{Entry::Note});
        V t = entries(Level::Top, c); // (Info… has one: Note in its place)
        assert(count(t, Entry::Info) == 0 && count(t, Entry::Note) == 1 && count(t, Entry::Details) == 1);
    }
    {
        Card c; c.get = true; c.installed = false;
        assert((entries(Level::Top, c) == V{Entry::Play, Entry::Details, Entry::Rearrange, Entry::SearchGames}));
        c.showDetails = false;
        assert((entries(Level::Top, c) == V{Entry::Play, Entry::Rearrange, Entry::SearchGames}));
    }
    assert(opens(Entry::Appearance) == Level::Appearance);
    assert(opens(Entry::MoveTo) == Level::MoveTo);
    assert(opens(Entry::Info) == Level::Info);
    assert(opens(Entry::More) == Level::More);
    assert(!opens(Entry::Play));
    assert(!opens(Entry::Delete));

    const Level groups[] = {Level::MoveTo, Level::Appearance, Level::Info, Level::More};
    const Entry groupEntry[] = {Entry::MoveTo, Entry::Appearance, Entry::Info, Entry::More};
    for (int m = 0; m < 1024; ++m) {
        Card c;
        c.get = m & 1; c.installed = m & 2; c.game = m & 4; c.homebrew = m & 8; c.hasId = m & 16;
        c.hasUpdate = m & 32; c.canClose = m & 64; c.showDetails = m & 128; c.canPanel = m & 256;
        c.deletable = m & 512;
        V top = entries(Level::Top, c);
        assert(top.size() <= 11); // (Play, then at most 10: five rows of two, none shrunk)
        assert(count(top, Entry::Details) == (c.showDetails ? 1 : 0));
        assert(!top.empty() && top.front() == Entry::Play);
        if (count(top, Entry::Delete)) assert(top.back() == Entry::Delete);
        for (int g = 0; g < 4; ++g) {
            V lv = entries(groups[g], c);
            bool inTop = count(top, groupEntry[g]) == 1;
            assert(count(top, groupEntry[g]) <= 1);
            if (groups[g] == Level::MoveTo) {
                assert(inTop == c.installed);
                continue;
            }
            assert(inTop == (lv.size() >= 2));
            for (Entry e : lv) {
                assert(count(lv, e) == 1);
                assert(count(top, e) + (inTop ? 1 : 0) == 1);
            }
        }
    }
    return 0;
}
