#pragma once
// What a card's + sheet shows: a top level and the groups it opens.
// Pure model (no UI); CardsActivity maps each Entry to its action.

#include <cstdint>
#include <optional>
#include <vector>

namespace cards::plus {

enum class Entry : std::uint8_t {
    Play, Update, CloseSoftware, Favourite, Rearrange, MoveTo, Appearance, Info, More, Delete,
    Rename, CardPicture, BackgroundPicture, TitlePicture, IconPicture, FullCard, DownloadPictures,
    Details, AboutGame, Note, CopyMusic,
    SearchGames, PanelButton, Hide
};

enum class Level : std::uint8_t { Top, MoveTo, Appearance, Info, More };

struct Card {
    bool get = false, installed = true, game = false, homebrew = false, hasId = true,
         hasUpdate = false, canClose = false, showDetails = true, canPanel = false, deletable = false;
};

inline std::optional<Level> opens(Entry e)
{
    switch (e) {
    case Entry::MoveTo: return Level::MoveTo;
    case Entry::Appearance: return Level::Appearance;
    case Entry::Info: return Level::Info;
    case Entry::More: return Level::More;
    default: return std::nullopt;
    }
}

inline std::vector<Entry> entries(Level level, const Card& c)
{
    std::vector<Entry> v;
    auto add = [&](bool when, Entry e) { if (when) v.push_back(e); };
    switch (level) {
    case Level::MoveTo:
        break;
    case Level::Appearance:
        add(!c.get, Entry::Rename);
        add(c.installed, Entry::CardPicture);
        add(c.installed, Entry::BackgroundPicture);
        add(c.installed, Entry::TitlePicture);
        add(c.installed, Entry::IconPicture);
        add(c.installed && c.homebrew, Entry::FullCard);
        add(c.installed, Entry::DownloadPictures);
        break;
    case Level::Info:
        add(c.installed && c.game, Entry::AboutGame);
        add(c.hasId && !c.get, Entry::Note);
        add(c.installed && c.game, Entry::CopyMusic);
        break;
    case Level::More:
        add(true, Entry::SearchGames);
        add(c.canPanel, Entry::PanelButton);
        add(c.installed, Entry::Hide);
        break;
    case Level::Top: {
        add(true, Entry::Play);
        add(c.hasUpdate, Entry::Update);
        add(c.canClose, Entry::CloseSoftware);
        add(c.showDetails, Entry::Details); // (on top, not in Info…: it's the one most used)
        add(c.hasId && !c.get, Entry::Favourite);
        add(true, Entry::Rearrange);
        add(c.installed, Entry::MoveTo);
        for (Level g : {Level::Appearance, Level::Info, Level::More}) {
            std::vector<Entry> sub = entries(g, c);
            if (sub.size() == 1) v.push_back(sub.front());
            else if (sub.size() > 1)
                v.push_back(g == Level::Appearance ? Entry::Appearance
                          : g == Level::Info       ? Entry::Info : Entry::More);
        }
        add(c.deletable, Entry::Delete);
        break;
    }
    }
    return v;
}

} // namespace cards::plus
