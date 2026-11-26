#pragma once
#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace cards {

struct CustomPage {
    std::string name;
    std::vector<std::string> titles;
    // The ones moved here (Settings > Custom pages > Move, not Add): off their
    // own page while they're here. Always a part of `titles`. When the page
    // goes (or is hidden) they're back on their own pages, as they were.
    std::vector<std::string> moved;
    // Made from the games' genres (Settings > Home > Genre pages): kept up to
    // date on its own, and taken away when that's turned off.
    bool genre = false;
};

// Writes `data` to `path` on CardsNX's writer thread (Library.cpp): the
// screen's thread never waits on the SD card, which an install or the
// pictures can keep busy for seconds. The newest data for a path wins.
void writeLater(const std::string& path, std::string data);
void flushWrites(); // everything written (before HOME closes)

// CardsNX's settings (the gear, or the Nintendo page > Settings), in
// sdmc:/config/CardsNX/settings.json.
struct Settings {
    static constexpr int kBuiltinPages = 4;
    static constexpr int kMaxCustomPages = 20; // (8 before the genre pages: every genre gets one)
    static constexpr int kMaxPages = kBuiltinPages + kMaxCustomPages;
    std::vector<CustomPage> customPages;
    bool notificationCards = true;
    bool lockGameBanner = true; // the playing game's background behind the lock screen's clock
    bool lockGameTitle = true;  // its title picture (or name) on the lock screen
    bool music = true;
    int musicVolume = 60;     // 0..100
    bool shuffle = false;
    bool repeat = false;      // the same song again
    bool sounds = true;
    int soundVolume = 80;     // 0..100
    bool players = true;      // Nextendo's green dots
    bool motion = true;       // animations (off: things move straight to place)
    bool bgMotion = true;     // the background art drifts
    bool gridView = false;    // Y: SwitchU's Wii U grid of icons instead of the card row
    bool xmbView = false;     // Y's third: the cross view (pages along the top, the page's games in a list)
    int rowStyle = 0;         // the row's look (no grid, no cross view): 0 cards, 2 wheel (1: looks since taken out, read as cards)
    int gridStyle = 0;        // the grid's look: 0 icons, 1 channels
    bool wheelLeft = false;   // Settings > Wheel: the rows down the left (the cover on the right)
    int wheelSize = 1;        // its rows: 0 small, 1 medium, 2 large
    bool channelScreen = true; // Settings > Channels: A opens the channel (big picture, Start), else starts it
    bool channelDisc = true;  // the channel's turning disc
    bool channelNames = false; // every tile's name (else the chosen one's)
    int channelTiles = 0;     // 0 4 x 3, 1 3 x 2 bigger
    int channelArt = 0;       // the tiles' picture: 0 each game's wide picture (else its icon), 1 the console's icon
    int startPage = 0;        // (old: the page order's first is where it opens now)
    std::vector<int> pageOrder = {0, 1, 2, 3}; // the pages in order: 0 games, 1 homebrew, 2 Nintendo, 3 media, then custom pages
    int lastPage = -1; // the custom page something was started from: back there afterwards
    int sort = 1;             // 0 last played, 1 title, 2 most played, 3 my order (Plus > Rearrange), 4 online players, 5 genre
    int haze = 45;            // how much the background art is softened, 0..80
    bool boxArt = true;       // box covers on cards (off: the eShop banner)
    int accent = 0;           // the focus colour (see kAccents)
    int glass = 1;            // 0 clear, 1 frosted, 2 dark
    int gameCardDisplay = 0;
    int gameCardIcons = 0;
    bool showInsertedGameCardIcon() const { return gameCardIcons >= 1; }
    bool showRequiredGameCardIcon() const { return gameCardIcons == 2; }
    int launchStyle = 0;      // starting a game: 0 simple fade, 1 splash, 2 card zoom, 3 card flip, 4 dive in, 5 off
    bool clock24 = true;
    bool showFree = true;     // free space in the status pill
    bool showTemp = true;     // the chip's temperature in the status pill (red and said when hot)
    bool titlePics = true;    // a 3D picture by the game's name in the panel
    // Homebrew moved by hand (+ > Move to Media / Move to Homebrew): .nro paths,
    // NRO forwarders' title IDs. Out: the ones Media takes by itself, kept out.
    std::vector<std::string> mediaIn, mediaOut;
    std::vector<std::string> gamesIn, gamesOut; // moved by hand onto / off the Games page (ports, ...)
    std::vector<std::string> fullCards;         // homebrew drawn as games' cards (+ > Show as a full card)
    std::uint64_t nroHash = 0;  // the .nro files at the last full read (homebrewHash)
    int titles = -1;            // the installed titles then (installedTitleCount)
    bool askUser = false;       // starting a game asks who's playing, as Nintendo's HOME does
    std::string panelApp = "news"; // the panel's right button: "news", "" nothing, else a card's id (hex)
    bool lockScreen = false;    // a lock screen when the console wakes
    bool lockMusic = false;     // the menu's music keeps playing on the lock screen
    bool keepHomebrew = true;   // HOME leaves a homebrew app open to resume, as a game (off: HOME closes it)
    std::string hbPath, hbHost; // the homebrew app left open and the game it runs as (HOME only)
    std::map<std::string, std::string> hiddenCards; // cards hidden with + (their ids and names): still installed
    int sheetBack = -1, sheetBackRow = 0; // Settings' tab and row while one of Nintendo's screens is open (HOME only)
    std::vector<int> hiddenPages; // pages not shown (ZL / ZR skip them; one always stays)
    bool spotify = false;       // the music player shows Spotify, not HOME music (HOME only)
    long long picturesAt = 0;   // when every game's pictures were last made sure of (seconds)
    long long achSeenAt = 0;    // the newest achievement HOME has said (seconds; 0: none said yet)
    bool introDone = false;     // the first-time intro was shown
    bool switchuOrder = false;  // SwitchU's own order (its layout.json) taken as My order, once
    bool forwardersOnGames = true; // installed NRO forwarders on the Games page (as the console's HOME and SwitchU)
    bool customPagesMove = false;  // + puts a game on a custom page by moving it there (off its own page), not a copy
    std::string wallpaper;      // one picture behind everything ("" = each game's art)
    std::vector<std::string> favourites; // starred with + (ids, hex): first on the Games page, whatever the sort
    bool nextendoAlerts = true;  // a notice when people come online in a game he has (Nextendo)
    bool playersMine = false;    // the players list: only games on the console
    bool genrePages = false;     // a page for each genre with 2+ of his games (as custom pages, made and kept up to date)
    int accentR = 100, accentG = 45, accentB = 45; // Highlight colour > Custom (each 0-100%)
    std::map<std::string, std::string> notes; // + > Note: a line of his own per game (id hex -> text)
    // Settings > Cross view.
    int crossSize = 1;            // the list's icons: 0 small, 1 medium, 2 large
    int crossInfo = 0;            // under the chosen game: 0 play time and last played, 1 play time, 2 last played, 3 nothing
    int crossBg = 0;              // behind: 0 the game's art, 1 dimmed, 2 dark
    int crossPos = 0;             // the list: 0 on the left, 1 in the middle
    bool crossNames = true;       // the categories' names under their icons
    bool crossSystemCat = true;   // the System category (last)
    bool crossPlusDetails = true; // + opens the details panel (off: the options, as on the cards)
    int crossStyle = 0;           // 0 the cross (pages along, games down), 1 a row (pages as tabs, games along: PS4-like)
    // Settings > Grid view.
    int gridLayout = 0;           // 0 5x3 (SwitchU's), 1 4x3, 2 4x2 big, 3 6x4 small
    int gridCursorColour = 0;     // 0 the highlight colour (White: SwitchU's blue), 1 always blue, 2 always the highlight
    bool gridPill = true;         // the chosen game's name under the grid
    bool gridDots = true;         // the page dots
    bool gridStars = true;        // favourites' stars on the tiles
    std::vector<std::string> musicOff, musicOrder; // Settings > Sound > Your music (song paths)
    std::map<std::string, std::vector<std::string>> playlists; // Settings > Sound > Playlists (name -> song paths, in order)
    std::string playlist;         // the one HOME plays ("" all songs)
    std::map<std::string, std::string> names; // + > Rename: by title ID (hex) or .nro path
    std::vector<std::string> sheetOrder; // Settings' tabs by name, in his order (new ones after)
    std::string user;         // who games start as (hex), "" = the last one who played
    std::string lastGame;     // the last game (or app) started from here, hex
    long long listsAt = 0;    // when the games and apps were last read in full (seconds since 1970)
    bool perfLog = false;      // sdmc:/config/CardsNX/perf.log: start-up times and slow frames
    std::string language;     // "es", "ja", ... ("" = the console's)
    std::vector<std::string> order[kMaxPages]; // My order, per page: ids (hex)

    void normalizePages() {
        if (customPages.size() > kMaxCustomPages) customPages.resize(kMaxCustomPages);
        const int count = kBuiltinPages + static_cast<int>(customPages.size());
        std::vector<int> valid;
        for (int page : pageOrder)
            if (page >= 0 && page < count && std::find(valid.begin(), valid.end(), page) == valid.end()) valid.push_back(page);
        for (int page = 0; page < count; ++page)
            if (std::find(valid.begin(), valid.end(), page) == valid.end()) valid.push_back(page);
        pageOrder = std::move(valid);
        hiddenPages.erase(std::remove_if(hiddenPages.begin(), hiddenPages.end(), [count](int page) {
            return page < 0 || page >= count;
        }), hiddenPages.end());
        std::sort(hiddenPages.begin(), hiddenPages.end());
        hiddenPages.erase(std::unique(hiddenPages.begin(), hiddenPages.end()), hiddenPages.end());
        if (static_cast<int>(hiddenPages.size()) == count) hiddenPages.erase(hiddenPages.begin());
    }

    static constexpr const char* kPath = "sdmc:/config/CardsNX/settings.json";
    static constexpr const char* kBackup = "sdmc:/config/CardsNX/settings.json.bak"; // the last one read whole
    static constexpr const char* kRestore = "sdmc:/config/CardsNX/settings.restore.json"; // put there by hand: taken once

    // Saving writes what's in memory over the file: only when that came from
    // it (or there was none). A file that couldn't be read left the defaults
    // here, and the next save wrote them over everything (2026-11-10: HOME out
    // of memory with Spotify playing reset every setting).
    bool readable = false;
    // The file was there but wasn't settings (cut short, not JSON): it was
    // put aside as kDamaged and these are the defaults. Said once (CardsActivity).
    bool startedFresh = false;
    static constexpr const char* kDamaged = "sdmc:/config/CardsNX/settings.json.bad";

    bool pinGameCard() const { return (gameCardDisplay & 1) != 0; }
    bool showGameCardTitle() const { return (gameCardDisplay & 2) != 0; }

    void load() {
        std::string text;
        if (read(kRestore, &text)) { // settings put back from outside (HOME would have saved over the file itself)
            readable = true;
            writeLater(kPath, text);
            writeLater(kBackup, std::move(text));
            std::remove(kRestore);
            normalizePages();
            return;
        }
        bool damaged = false, backupDamaged = false;
        readable = read(kPath, &text, &damaged);
        if (readable) {
            writeLater(kBackup, std::move(text));
        } else {
            readable = read(kBackup, nullptr, &backupDamaged); // the last good copy
            if (!readable) {
                std::error_code e1, e2;
                const bool have = std::filesystem::exists(kPath, e1), haveBackup = std::filesystem::exists(kBackup, e2);
                readable = !e1 && !e2 && !have && !haveBackup; // none at all: a first start, these are its settings
                // There, but not settings (and no good copy): kept aside, and the
                // defaults saved from now on. Without this nothing was ever saved
                // again, silently, until the file was deleted by hand. A read that
                // failed some other way (out of memory) still saves nothing.
                if (!readable && !e1 && !e2 && (!have || damaged) && (!haveBackup || backupDamaged) && (have || haveBackup)) {
                    *this = Settings{};
                    std::error_code e;
                    std::filesystem::rename(have ? kPath : kBackup, kDamaged, e);
                    readable = startedFresh = true;
                }
            }
        }
        normalizePages();
    }

    bool read(const char* path, std::string* textOut, bool* damaged = nullptr) {
        try {
            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const auto j = nlohmann::json::parse(text);
            if (!j.is_object()) {
                if (damaged) *damaged = true;
                return false;
            }
            music = j.value("music", music);
            musicVolume = j.value("musicVolume", musicVolume);
            shuffle = j.value("shuffle", shuffle);
            repeat = j.value("repeat", repeat);
            sounds = j.value("sounds", sounds);
            soundVolume = j.value("soundVolume", soundVolume);
            players = j.value("players", players);
            motion = j.value("motion", motion);
            bgMotion = j.value("bgMotion", bgMotion);
            gridView = j.value("gridView", gridView);
            xmbView = j.value("xmbView", xmbView);
            rowStyle = std::clamp(j.value("rowStyle", rowStyle), 0, 2);
            gridStyle = std::clamp(j.value("gridStyle", gridStyle), 0, 1);
            wheelLeft = j.value("wheelLeft", wheelLeft);
            wheelSize = std::clamp(j.value("wheelSize", wheelSize), 0, 2);
            channelScreen = j.value("channelScreen", channelScreen);
            channelDisc = j.value("channelDisc", channelDisc);
            channelNames = j.value("channelNames", channelNames);
            channelTiles = std::clamp(j.value("channelTiles", channelTiles), 0, 1);
            channelArt = std::clamp(j.value("channelArt", channelArt), 0, 1);
            startPage = j.value("startPage", startPage);
            pageOrder = j.value("pageOrder", pageOrder);
            customPages.clear();
            if (j.contains("customPages") && j["customPages"].is_array())
                for (const auto& page : j["customPages"]) {
                    if (!page.is_object() || !page.contains("name") || !page["name"].is_string()) continue;
                    CustomPage custom;
                    custom.name = page["name"].get<std::string>();
                    if (custom.name.empty()) continue;
                    if (page.contains("titles") && page["titles"].is_array())
                        for (const auto& id : page["titles"])
                            if (id.is_string() && std::find(custom.titles.begin(), custom.titles.end(), id.get<std::string>()) == custom.titles.end())
                                custom.titles.push_back(id.get<std::string>());
                    if (page.contains("moved") && page["moved"].is_array())
                        for (const auto& id : page["moved"])
                            if (id.is_string() && std::find(custom.titles.begin(), custom.titles.end(), id.get<std::string>()) != custom.titles.end()
                                && std::find(custom.moved.begin(), custom.moved.end(), id.get<std::string>()) == custom.moved.end())
                                custom.moved.push_back(id.get<std::string>());
                    custom.genre = page.value("genre", false);
                    customPages.push_back(std::move(custom));
                    if (customPages.size() == kMaxCustomPages) break;
                }
            notificationCards = j.value("notificationCards", notificationCards);
            lastPage = j.value("lastPage", lastPage);
            lockGameBanner = j.value("lockGameBanner", lockGameBanner);
            lockGameTitle = j.value("lockGameTitle", lockGameTitle);
            sort = j.value("sortOrder", sort);
            haze = j.value("haze", haze);
            boxArt = j.value("boxArt", boxArt);
            accent = j.value("accent", accent);
            glass = j.value("glass", glass);
            gameCardDisplay = std::clamp(j.value("gameCardDisplay", gameCardDisplay), 0, 3);
            gameCardIcons = std::clamp(j.value("gameCardIcons", gameCardIcons), 0, 2);
            launchStyle = j.value("launchStyle", launchStyle);
            clock24 = j.value("clock24", clock24);
            showFree = j.value("showFree", showFree);
            showTemp = j.value("showTemp", showTemp);
            titlePics = j.value("titlePics", titlePics);
            mediaIn = j.value("mediaIn", mediaIn);
            mediaOut = j.value("mediaOut", mediaOut);
            gamesIn = j.value("gamesIn", gamesIn);
            gamesOut = j.value("gamesOut", gamesOut);
            fullCards = j.value("fullCards", fullCards);
            nroHash = j.value("nroHash", nroHash);
            titles = j.value("titles", titles);
            askUser = j.value("askUser", askUser);
            panelApp = j.value("panelApp", panelApp);
            lockScreen = j.value("lockScreen", lockScreen);
            lockMusic = j.value("lockMusic", lockMusic);
            keepHomebrew = j.value("keepHomebrew", keepHomebrew);
            hbPath = j.value("hbPath", hbPath);
            hbHost = j.value("hbHost", hbHost);
            hiddenCards = j.value("hiddenCards", hiddenCards);
            sheetBack = j.value("sheetBack", sheetBack);
            sheetBackRow = j.value("sheetBackRow", sheetBackRow);
            hiddenPages = j.value("hiddenPages", hiddenPages);
            spotify = j.value("spotify", spotify);
            picturesAt = j.value("picturesAt", picturesAt);
            achSeenAt = j.value("achSeenAt", achSeenAt);
            introDone = j.value("introDone", introDone);
            switchuOrder = j.value("switchuOrder", switchuOrder);
            forwardersOnGames = j.value("forwardersOnGames", forwardersOnGames);
            customPagesMove = j.value("customPagesMove", customPagesMove);
            wallpaper = j.value("wallpaper", wallpaper);
            favourites = j.value("favourites", favourites);
            nextendoAlerts = j.value("nextendoAlerts", nextendoAlerts);
            playersMine = j.value("playersMine", playersMine);
            genrePages = j.value("genrePages", genrePages);
            if (j.contains("notes") && j["notes"].is_object()) notes.clear();
            if (j.contains("notes") && j["notes"].is_object())
                for (auto it = j["notes"].begin(); it != j["notes"].end(); ++it)
                    if (it.value().is_string()) notes[it.key()] = it.value().get<std::string>();
            crossSize = std::clamp(j.value("crossSize", crossSize), 0, 2);
            crossInfo = std::clamp(j.value("crossInfo", crossInfo), 0, 3);
            crossBg = std::clamp(j.value("crossBg", crossBg), 0, 2);
            crossPos = std::clamp(j.value("crossPos", crossPos), 0, 1);
            crossNames = j.value("crossNames", crossNames);
            crossSystemCat = j.value("crossSystemCat", crossSystemCat);
            crossPlusDetails = j.value("crossPlusDetails", crossPlusDetails);
            crossStyle = std::clamp(j.value("crossStyle", crossStyle), 0, 1);
            gridLayout = std::clamp(j.value("gridLayout", gridLayout), 0, 3);
            gridCursorColour = std::clamp(j.value("gridCursorColour", gridCursorColour), 0, 2);
            gridPill = j.value("gridPill", gridPill);
            gridDots = j.value("gridDots", gridDots);
            gridStars = j.value("gridStars", gridStars);
            musicOff = j.value("musicOff", musicOff);
            musicOrder = j.value("musicOrder", musicOrder);
            if (j.contains("playlists") && j["playlists"].is_object()) {
                playlists.clear();
                for (auto it = j["playlists"].begin(); it != j["playlists"].end(); ++it)
                    if (it.value().is_array()) playlists[it.key()] = it.value().get<std::vector<std::string>>();
            }
            playlist = j.value("playlist", playlist);
            accentR = std::clamp(j.value("accentR", accentR), 0, 100);
            accentG = std::clamp(j.value("accentG", accentG), 0, 100);
            accentB = std::clamp(j.value("accentB", accentB), 0, 100);
            names = j.value("names", names);
            sheetOrder = j.value("sheetOrder", sheetOrder);
            user = j.value("user", user);
            lastGame = j.value("lastGame", lastGame);
            listsAt = j.value("listsAt", listsAt);
            perfLog = j.value("perfLog", perfLog);
            language = j.value("language", language);
            const char* const keys[4] = {"orderGames", "orderHomebrew", "orderNintendo", "orderMedia"};
            for (int p = 0; p < 4; ++p) order[p] = j.value(keys[p], order[p]);
            if (textOut) *textOut = std::move(text);
            return true;
        } catch (const nlohmann::json::exception&) { // not JSON, or not ours (cut short)
            if (damaged) *damaged = true;
            return false;
        } catch (...) { // (out of memory, say: the file may be fine)
            return false;
        }
    }

    mutable unsigned saves = 0; // how many times it was saved (a way back from before a change is dropped)
    void save() const {
        ++saves;
        if (!readable) return; // (see readable)
        writeLater(kPath, text());
    }

    // The settings as the file has them (save; a backup).
    std::string text() const {
        nlohmann::json j = {{"music", music}, {"musicVolume", musicVolume}, {"shuffle", shuffle}, {"repeat", repeat},
                            {"sounds", sounds}, {"soundVolume", soundVolume}, {"players", players},
                            {"motion", motion}, {"bgMotion", bgMotion}, {"gridView", gridView}, {"xmbView", xmbView}, {"startPage", startPage}, {"pageOrder", pageOrder},
                            {"sortOrder", sort}, {"haze", haze}, {"boxArt", boxArt}, {"accent", accent},
                            {"glass", glass}, {"launchStyle", launchStyle}, {"clock24", clock24}, {"showFree", showFree}, {"showTemp", showTemp}, {"titlePics", titlePics}, {"mediaIn", mediaIn}, {"mediaOut", mediaOut}, {"gamesIn", gamesIn}, {"gamesOut", gamesOut}, {"fullCards", fullCards}, {"nroHash", nroHash}, {"titles", titles}, {"askUser", askUser}, {"panelApp", panelApp},
                            {"lockScreen", lockScreen}, {"lockMusic", lockMusic}, {"keepHomebrew", keepHomebrew}, {"hbPath", hbPath}, {"hbHost", hbHost}, {"hiddenCards", hiddenCards}, {"sheetBack", sheetBack}, {"sheetBackRow", sheetBackRow}, {"hiddenPages", hiddenPages}, {"spotify", spotify}, {"picturesAt", picturesAt}, {"achSeenAt", achSeenAt},{"introDone", introDone}, {"switchuOrder", switchuOrder}, {"forwardersOnGames", forwardersOnGames}, {"customPagesMove", customPagesMove}, {"wallpaper", wallpaper}, {"names", names},
                            {"sheetOrder", sheetOrder}, {"user", user}, {"lastGame", lastGame}, {"listsAt", listsAt}, {"perfLog", perfLog}, {"language", language},
                            {"orderGames", order[0]}, {"orderHomebrew", order[1]}, {"orderNintendo", order[2]},
                            {"orderMedia", order[3]}};
        j["customPages"] = nlohmann::json::array();
        for (const auto& page : customPages) {
            nlohmann::json p = {{"name", page.name}, {"titles", page.titles}, {"moved", page.moved}};
            if (page.genre) p["genre"] = true;
            j["customPages"].push_back(std::move(p));
        }
        j["notificationCards"] = notificationCards;
        j["favourites"] = favourites;
        j["nextendoAlerts"] = nextendoAlerts;
        j["playersMine"] = playersMine;
        j["genrePages"] = genrePages;
        j["notes"] = notes;
        j["crossSize"] = crossSize;
        j["crossInfo"] = crossInfo;
        j["crossBg"] = crossBg;
        j["crossPos"] = crossPos;
        j["crossNames"] = crossNames;
        j["crossSystemCat"] = crossSystemCat;
        j["crossPlusDetails"] = crossPlusDetails;
        j["crossStyle"] = crossStyle;
        j["gridLayout"] = gridLayout;
        j["rowStyle"] = rowStyle;
        j["gridStyle"] = gridStyle;
        j["wheelLeft"] = wheelLeft;
        j["wheelSize"] = wheelSize;
        j["channelScreen"] = channelScreen;
        j["channelDisc"] = channelDisc;
        j["channelNames"] = channelNames;
        j["channelTiles"] = channelTiles;
        j["channelArt"] = channelArt;
        j["gridCursorColour"] = gridCursorColour;
        j["gridPill"] = gridPill;
        j["gridDots"] = gridDots;
        j["gridStars"] = gridStars;
        j["musicOff"] = musicOff;
        j["musicOrder"] = musicOrder;
        j["playlists"] = playlists;
        j["playlist"] = playlist;
        j["accentR"] = accentR;
        j["accentG"] = accentG;
        j["accentB"] = accentB;
        j["lastPage"] = lastPage;
        j["lockGameBanner"] = lockGameBanner;
        j["lockGameTitle"] = lockGameTitle;
        j["gameCardDisplay"] = gameCardDisplay;
        j["gameCardIcons"] = gameCardIcons;
        return j.dump(2);
    }
};

} // namespace cards
