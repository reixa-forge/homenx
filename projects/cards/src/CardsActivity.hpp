#pragma once
#include "Audio.hpp"
#include "GameMusic.hpp"
#include "HomeLayout.hpp"
#include "Library.hpp"
#include "Mtp.hpp"
#include "PlusMenu.hpp"
#include "Settings.hpp"
#include <switchu/lock_screen_policy.hpp>
#include "Spotify.hpp"
#ifdef SWITCHU_MENU
#include "core/SwitifyClient.hpp" // Switify: Spotify played by the console itself
#endif

#include <nxui/Activity.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/Theme.hpp>

#ifdef SWITCHU_MENU
#include "ReleaseUpdater.hpp"
#include "launcher/AppletLauncher.hpp"
#include "settings/TextEntryScreen.hpp"
#include "social/DiscordService.hpp"
#include "widgets/DiscordPanel.hpp"
#include "widgets/SocialScreen.hpp"
#include "settings/ControllerTestScreen.hpp"
#include <switchu/lock_password.hpp>
#endif

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cards {

// CardsNX: a HOME screen. A row of box-art cards over the chosen game's art, a
// status bar on top (menu, Discord, sort, a pill with you, the time, battery,
// free space and Nextendo players, notifications, music, power, settings) and
// a glass panel with the game's details and Play below. Three pages: Games,
// Homebrew (.nro apps and NRO forwarders) and Nintendo (News or Switch Online,
// eShop, Album, ...).
//
// As an .nro it runs over hbl. Built into HomeNX's menu (SWITCHU_MENU) it is
// HOME itself, chosen in NXToggle: games, applets and power go through HomeNX's
// daemon, and Discord (L), News and updates come from HomeNX.
class CardsActivity final : public nxui::Activity {
public:
    // As HOME: what the daemon said at start (a game suspended, one running,
    // the console just booted).
    explicit CardsActivity(std::uint64_t suspendedId = 0, bool appRunning = false, bool atBoot = true,
                           bool fromApplet = false);
    void setWakeStart(bool wake) { m_wakeStart = wake; }
    bool onCreate() override;
#ifdef SWITCHU_MENU
    bool presentInitialFrame(nxui::Renderer& ren) override;
#endif
    void onDestroy() override;
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer&) override {}
    void onAfterPresent(nxui::Renderer&) override;
    // Render tiers (nxui::Application): every frame while something moves on
    // its own; the decorations (pulses, drifts) stand still in deep idle.
    bool wantsFullRate() const override;
    bool supportsDeepIdle() const override;
    void onIdleDeep(bool deep) override;
    void refreshStatusNow(); // the bar's clock changed: worker 1 reads it at once
    nxui::Widget* focusRoot() override; // HomeNX's screens when one is open; else it reads the buttons itself

private:
    enum class Zone { Top, Cards, Bottom };
    enum Page { kGames, kHomebrew, kNintendo, kMedia, kPageCount };
    int pageCount() const { return kPageCount + static_cast<int>(m_settings.customPages.size()); }
    std::string pageName(int page) const;
    void rebuildCustomPages();
    void openCustomPages();
    void openPagePicker();
    void editCustomPage(int page);
    void addToCustomPage(const Game& game); // + > Move to… > Add a copy to a page…
    void openStorage();
    void readStorage(bool withTitles = false);
    bool m_storageWantTitles = false;
    std::vector<std::string> m_lockPlaylist;
    bool m_lockRotate = false, m_milestonesOn = true, m_packsOn = true; // (files: read when Settings opens, not every frame)
    void moveSoftware(const Game& game, bool toSd);
    bool softwareBusy(std::uint64_t id) const;
    void updateStorage();
    void drawNotificationCard(nxui::Renderer& renderer);
    void notifyCard(std::string title, std::string message, std::string glyph = "news");
    // The top bar's buttons, left to right.
    enum Top { kTopMenu, kTopDiscord, kTopSort, kTopUser, kTopPlayers, kTopBell, kTopMusic, kTopPower, kTopGear,
               kTopCount };
    // kLaunch: the art at full size, for the launch; kStock: the console's own icon (the grid);
    // kTile: the background picture at a card's size (Channels' tiles: a whole page of them).
    enum What { kCard, kBackground, kIcon, kLaunch, kStock, kTile };
    struct Art {
        std::unique_ptr<nxui::Texture> card, bg, icon;
        ArtKind kind = ArtKind::Banner, bgKind = ArtKind::Banner, iconKind = ArtKind::Icon;
        bool cardAsked = false, bgAsked = false, iconAsked = false;
        std::unique_ptr<nxui::Texture> stock; // the console's own icon (the grid's tiles)
        bool stockAsked = false;
        std::unique_ptr<nxui::Texture> tile;  // kTile
        ArtKind tileKind = ArtKind::Banner;
        bool tileAsked = false;
        float tileIn = -10.f;
        float bgIn = -10.f;                    // when its background arrived (it sharpens in)
        std::uint64_t lastAccess = 0, selectedTick = 0;
        float cardIn = -10.f; // when it arrived (it fades in)
        float retryAt = 0.f;  // nothing came: asked again after this
        // Whether a picture of this kind was asked for (kLaunch: none).
        bool* askedFor(int what) {
            switch (what) {
            case kCard: return &cardAsked;
            case kBackground: return &bgAsked;
            case kIcon: return &iconAsked;
            case kStock: return &stockAsked;
            case kTile: return &tileAsked;
            default: return nullptr;
            }
        }
    };
    // The lists, read on a thread (HOME shows last time's at once).
    struct Lists {
        std::vector<Game> games, homebrew;
        float ms = 0.f;         // how long reading them took
        bool gamesOnly = false; // the installed titles only: the .nro apps stay as they are
        int titles = -1;        // installed titles (Settings.titles)
        std::uint64_t nroHash = 0;
    };
    // Back from a game, last time's lists show; this checks quickly on a thread
    // whether apps or games were added meanwhile (then they're read again).
    struct Changes {
        int titles = -1;
        std::uint64_t nroHash = 0;
    };
    std::future<Changes> m_changesJob;
    float m_changesIn = 10.f; // the next look for new software (installed titles, .nro files)
    bool m_listsStale = false; // the first look after a start, sooner than the regular ones
    float m_lastActivity = 0.f; // when something was last pressed (pictures for every game wait for quiet)
    int m_changesChecks = 0;
    // Every game and .nro (slow: seconds), on whatever thread calls it; or only
    // the installed titles (a game installed, updated or removed).
    // knownNroHash: the .nro apps' paths as last read (Settings.nroHash): the same, they aren't read again.
    static Lists readAllLists(bool gamesOnly = false, std::uint64_t knownNroHash = 0);
    struct Job {
        Game game;
        int what = kCard;
        int gen = 0; // m_artGen when asked (see forgetAllArt)
        bool spare = false; // read ahead while nothing else is wanted: only into free picture memory
    };
    struct Done {
        std::uint64_t id = 0;
        int what = kCard;
        ArtKind kind = ArtKind::Banner;
        std::vector<std::uint8_t> rgba;
        int w = 0, h = 0;
        bool again = false; // an icon for now: its title picture is asked for again
        bool spare = false; // (Job::spare)
        std::vector<std::uint8_t> small; // a cover's very small copy (m_thumbs)
        int sw = 0, sh = 0;
        int gen = 0;
    };
    // Bumped when every picture is let go (a Pictures setting changed): what was
    // being made for the old setting is dropped when it arrives.
    int m_artGen = 0;
#ifndef CARDS_ARTWORK_GPU_MB
#define CARDS_ARTWORK_GPU_MB 72
#endif
    static constexpr std::uint64_t kArtworkBudget = CARDS_ARTWORK_GPU_MB * 1024ull * 1024ull;
    std::uint64_t m_artAccess = 0;
    // Room for a picture: kYes now; kLater when one was let go and its memory
    // frees a few frames on (the picture waits for it); kNever when nothing can go.
    enum class Admit { kYes, kLater, kNever };
    Admit reserveArtwork(std::uint64_t bytes, bool stock = false, bool spare = false);
    float m_spareFullUntil = 0.f; // a read-ahead picture found no room: none asked for until then

    // The queue is cleared (a page switch, a new order): what was asked and isn't
    // here yet is asked for again near the new selection.
    void requeueArt();
    // A row in a popup or in Settings.
    struct MenuItem {
        enum Kind { Action, Toggle, Choice, Slider, Info, Header }; // Header: a heading over a group of Settings' rows
        Kind kind = Action;
        std::string label, detail;              // detail: a line under it (Settings)
        std::function<void()> run;              // Action
        std::function<std::string()> value;     // Choice / Info (and an Action's note)
        std::function<void(int)> change;        // Choice, Slider: -1 / +1; Toggle: flip
        std::function<float()> level;           // Toggle 0 / 1, Slider 0..1
        const nxui::Texture* picture = nullptr; // a user's picture
        bool danger = false;                    // red (Delete)
        bool stay = false;                      // an action that keeps the popup open
        const char* glyph = nullptr;            // its icon (Plus's options)
        std::function<void()> onX, onY;         // X / Y on the row (a popup's list: the list stays open)
        std::function<const char*()> xHint, yHint; // their names in the bar at the bottom ("Listen", "Stop")
        const char* aHint = nullptr;            // A's name there, if not "Choose"
        float anim = -1.f;                      // a toggle's knob, easing
    };
    struct Section {
        std::string name;
        const char* icon;
        std::vector<MenuItem> rows;
    };

    static float sheetRowY(const std::vector<MenuItem>& rows, size_t index); // how far down a row of Settings starts

    static MenuItem action(std::string label, std::function<void()> run, bool danger = false);
    static MenuItem toggle(std::string label, std::function<bool()> get, std::function<void()> flip,
                           std::string detail = {});
    static MenuItem choice(std::string label, std::function<std::string()> value, std::function<void(int)> change,
                           std::string detail = {});
    static MenuItem slider(std::string label, std::function<int()> get, std::function<void(int)> set,
                           std::string detail = {});
    static MenuItem info(std::string label, std::function<std::string()> value);

    std::vector<Game>& pageGames(int page) { return m_gameCardPinned ? m_gameCardPages[page] : m_pages[page]; }
    std::vector<Game>& games() { return pageGames(m_page); }
    // Y: SwitchU's Wii U grid (Settings.gridView) instead of the card row, drawn
    // and moving as SwitchU's does (IconGrid, GlossyIcon, SelectionCursor,
    // TitlePillWidget, PageIndicator and its page arrows, Default Dark): 5 x 3
    // glass tiles with the console's own icons, pages that slide (0.34 s),
    // the edge bump, a cursor that glides between tiles, the name pill and dots.
    // The grid's layout (Settings > Grid view > Layout): columns, rows, the tile and its spacing.
    int m_gridCols = 5, m_gridRows = 3, m_gridPer = 15;
    float m_gridCellW = 150.f, m_gridCellH = 150.f; // a tile (Channels: wide)
    float m_gridPadX = 20.f, m_gridPadY = 16.f, m_gridOriginX = 225.f, m_gridOriginY = 119.f;
    void applyGridLayout();
    bool gridOn() const { return m_forceLayout >= 0 ? m_forceLayout == 1 : m_settings.gridView; }
    // Y: the row and the grid morph into each other (IconGrid's layout morph,
    // 0.40 s inOutCubic): m_gridIn goes 0 (the row) to 1 (the grid) evenly.
    bool morphing() const { return m_gridIn > 0.f && m_gridIn < 1.f; }
    int m_forceLayout = -1; // (cardRect: either layout's place, while they morph)
    // The row's other looks (Settings.rowStyle, HomeLayout.hpp): the same games,
    // selection and scroll, laid out as a wheel; and the grid's
    // Channels style. Y / Settings > Look > Home layout choose (setLayout).
    bool wheelOn() const { return !gridOn() && !m_settings.xmbView && m_settings.rowStyle == layout::kRowWheel; }
    bool channelsOn() const { return gridOn() && m_settings.gridStyle == layout::kGridChannels; }
    float rowPitch() const; // touch: pixels dragged per card (the wheel: down, the rest: across)
    // The wheel as Settings > Wheel has it (row size, which side).
    float wheelScale() const { return layout::wheelSize(m_settings.wheelSize); }
    layout::WheelSlot wheelAt(float rel) const { return layout::wheelSlot(rel, wheelScale(), m_settings.wheelLeft); }
    nxui::Rect wheelCoverRect(float slide) const {
        const layout::Box b = layout::wheelCover(m_settings.wheelLeft);
        return {b.x + slide, b.y, b.w, b.h};
    }
    void setLayout(layout::Layout to);
    bool noPanel() const { return wheelOn(); } // (its name and details are drawn its own way)
    void drawWheel(nxui::Renderer& r, bool rows); // false: the cover (under the glass); true: the rows (glass, over it)
    void toggleGrid();
    // The cross view (Y's third look): the pages side by side along the top,
    // then System; the chosen page's games in a list going down.
    void cycleView();                 // Y: the next home layout (layout::next)
    void setCrossView(bool on);
    bool updateCross(float dt);       // its buttons (false: the bar has them)
    void drawCross(nxui::Renderer& r);
    void drawCrossList(nxui::Renderer& r);
    // Settings > Cross view > Style: Row (PS4-like): the pages as tabs along
    // the top, the page's games in one row of tiles, the chosen one big with
    // its name and facts under it.
    void drawCrossRow(nxui::Renderer& r);
    bool crossRowTouch(float x, float y);
    float m_rowScroll = 0.f; // (the row's first tile shown, easing)
    bool m_rowTabs = false;  // the row: up from the games is on the page tabs (left / right change page)
    float m_rowTabX = 0.f;   // the tabs' scroll (the chosen one kept on screen), easing
    float m_rowZRepeat = 0.f;
    std::uint64_t m_rowInfoAsked = 0;
    void rowTabPlaces(std::vector<float>& x, std::vector<float>& w); // each tab's place along the strip (unscrolled)
    bool crossTouch(float x, float y);
    std::vector<int> crossPages() const;  // the pages shown, in your order
    int crossCat() const;                 // the category chosen (crossPages().size(): System)
    void crossCatTo(int cat);
    struct CrossItem { std::string label, glyph; std::function<std::string()> value; std::function<void()> run; };
    std::vector<CrossItem> crossSystem();
    bool m_crossSystem = false;   // System chosen
    int m_crossSysSel = 0;
    float m_crossCat = 0.f, m_crossRow = 0.f, m_crossSysRow = 0.f; // drawn (they ease to the chosen)
    float m_crossDrawnAt = 0.f, m_crossRepeat = 0.f, m_crossVRepeat = 0.f;
    float m_crossListIn = 1.f;    // the list fading in after the category changed
    float crossX() const;         // the chosen column's middle (Settings > Cross view > List position)
    float crossScale() const;     // the list's icons (Settings > Cross view > Icon size)
    // The details panel (+ in the cross view): the game's art and all that's
    // known about it, and what can be done with it.
    bool m_crossInfo = false;
    float m_crossInfoIn = 0.f;    // its slide (0 out, 1 in)
    int m_crossInfoSel = 0;
    struct CrossFacts { std::uint64_t id = 0; int achEarned = 0, achTotal = 0; };
    CrossFacts m_crossFacts;      // (achievements read from the SD card once per game, not each frame)
    void openCrossInfo();
    void crossInfoFacts();
    std::vector<CrossItem> crossInfoActions();
    void updateCrossInfo();
    void drawCrossInfo(nxui::Renderer& r);
    void editNote(const Game& game);
    // Channels: A on a channel opens it (the Wii's channel screen): its picture
    // large, a disc turning for a game, Back and Play; L / R the one before / after.
    bool m_chanOpen = false;
    float m_chanIn = 0.f;   // its zoom in (0 out, 1 in)
    int m_chanSel = 1;      // 0 Back, 1 Play
    float m_chanSwap = 1.f; // L / R: the new picture sliding in (1: there)
    int m_chanDir = 0;      // from which side
    float m_chanSpin = 0.f; // the disc's turn (0..1)
    float m_chanAt = 0.f;   // when it was last drawn (its steps go by the time since)
    bool channelScreenOn() const { return channelsOn() && m_settings.channelScreen && !m_moving; }
    void openChannel();
    void closeChannel(bool quiet = false);
    void channelPageCatchUp();
    void channelStep(int dir);
    void channelStart();
    void updateChannel();
    bool channelTouch(float x, float y);
    void drawChannel(nxui::Renderer& r);
    // The eShop's facts about each game (Library's GameInfo): the screen's copy,
    // and the new ones from worker 0 (under m_mu).
    std::map<std::uint64_t, GameInfo> m_infos, m_infosIn;
    void wantInfo(std::uint64_t id);      // asked first (the panel is open on it)
    int m_crossInfoScroll = 0;            // the description's first line shown
    float m_barLift = 0.f;                // the top bar moved up by (the details panel: off the screen)
    // Custom icons: + > Icon picture, and the cross view's categories.
    bool m_pickIconMode = false, m_pickIconMine = false, m_pickSaveIcon = false;
    std::string m_pickIconKey, m_pickSaveIconKey;
    void openIconPicker(const Game& g, const std::string& key, bool mineOnly, const std::string& query = {});
    std::map<std::string, std::unique_ptr<nxui::Texture>> m_customIconTex;
    std::unordered_set<std::string> m_customIconNone;
    const nxui::Texture* customIcon(const std::string& key); // (read once; "" none)
    void openCategoryIcons();
    void openMusicManager(int chosen = 0); // Settings > Sound > Your music
    void openSongMenu(const std::string& path, int at);
    // Playlists (Settings > Sound > Playlists): named lists of songs, one played.
    void applyMusicList();                // the playlist chosen (or all songs) to Audio
    void openPlaylists(int chosen = 0);
    void openPlaylist(const std::string& name);
    void openPlaylistAdd(const std::string& name, int chosen = 0);
    void openAddToPlaylist(const std::string& path);
    void newPlaylist(std::function<void(const std::string&)> then);
    // A game's own music (+ > Copy its music): copied to /music/<game> in the background.
    // First its files are opened and its songs listed; he chooses, hears and
    // renames them (openSongPicker); then the chosen ones are copied. One job
    // on m_gmThread at a time: 1 opening, 2 reading a song to hear, 3 copying.
    std::unique_ptr<gamemusic::Progress> m_gm;
    std::unique_ptr<gamemusic::GameFiles> m_gmFiles; // the game's songs (while he chooses, and copying)
    std::thread m_gmThread;
    std::string m_gmGame;
    std::uint64_t m_gmId = 0;
    std::atomic<int> m_gmJob{0};
    std::atomic<bool> m_gmJobDone{false};
    std::mutex m_gmMu;                                  // the job's answer:
    std::unique_ptr<gamemusic::GameFiles> m_gmOpened;   //  (1) the files
    std::string m_gmWhy;                                //  (1, 2) why not
    std::vector<std::uint8_t> m_gmHeard;                //  (2) the song's bytes
    std::unique_ptr<gamemusic::WavStream> m_gmHeardStream; //  (2) or it, decoded as it plays
    int m_gmHearing = -1;                               // the song being heard
    std::string m_gmPickerTitle;
    void copyGameMusic(const Game& game);
    void gameMusicJob(int job, std::function<void()> work);
    void checkGameMusicJob();
    void openSongPicker(int chosen = 0);
    void openSongOptions(int song);
    void listenToSong(int song);
    void checkGameMusic();
    void renameSong(const std::string& path, int at);
    nxui::Color gridCursor(float a) const; // the grid's cursor: the highlight colour (White: SwitchU's blue)
    void drawGrid(nxui::Renderer& r);
    void updateGrid(float dt);
    bool gridMove(int dx, int dy);  // false when the move leaves the grid (up from the top row)
    void gridPage(int dir, bool fromEdge); // L / R (and the row's ends): the next page
    void gridPages(int count);              // a flick: that many pages on (- back), the same cell
    void gridSlideTo(int page);
    float gridPageDx(int page) const;
    nxui::Rect gridTileRect(int index) const; // its cell, with the slide (no focus scale)
    float gridFocusScale(int index) const;
    float gridFocusGlow(int index) const;
    float m_gridIn = 0.f;           // 0 the card row, 1 the grid (the panel fades with it)
    int m_gridShown = 0, m_gridFrom = 0, m_gridDir = 1; // IconGrid's page, the one leaving
    float m_gridSlideT = 1.f;       // 1: not sliding
    float m_gridBump = 0.f, m_gridBumpT = -1.f;
    int m_gridBumpDir = 0;
    int m_gridFocus = -1, m_gridUnfocus = -1; // GlossyIcon focus: grows 1.075, the last one shrinks back
    float m_gridFocusT = 1.f, m_gridUnfocusT = 1.f;
    float m_gridPulse = 0.f;        // GlossyIcon's m_suspendPulse
    nxui::Rect m_curFrom{}, m_curTo{}, m_curRect{};
    float m_curT = 1.f, m_curDur = 0.2f, m_curTeleport = 0.f, m_curWave = 0.f;
    bool m_curInit = false;
    std::string m_pillText, m_pillShown;
    float m_pillXFrom = 640.f, m_pillWFrom = 0.f, m_pillXTo = 640.f, m_pillWTo = 0.f, m_pillT = 1.f, m_pillReveal = 1.f;
    int m_dotsPage = -1;
    float m_dotsPop = 0.30f;
    float m_arrowShowL = 0.f, m_arrowShowR = 0.f, m_arrowKickL = 0.f, m_arrowKickR = 0.f;
    nxui::Font m_fGridPill, m_fGridGlyph;
    std::unique_ptr<nxui::Texture> m_gridArrowL, m_gridArrowR;
    bool m_gridAssetsTried = false;
    float m_gridDrag = 0.f;         // a swipe in progress (pixels)
    // Touch on the grid moves the pages on a spring: let go, they carry on from
    // where the finger left them, at its speed (m_gridSettle eases to 0); a new
    // touch catches them where they are (m_gridGrab).
    float m_gridSettle = 0.f, m_gridSettleV = 0.f, m_gridGrab = 0.f;
    void gridTouchRelease(float offset, float speed);
    // L / R on the card row: a screenful of cards at a time.
    static constexpr int kCardsPerScreen = 5;

    int& sel() { return m_sels[m_page]; }
    const Game* current();

    void worker(int index);
    void wantArt();
    void takeArt();
    void forgetAllArt(bool files = true); // files: the cached pictures go too (else they're only shown again)
    std::future<long> m_keyCheckJob;      // Settings > Look > SteamGridDB key: does SteamGridDB take it?
    std::future<long> m_reportJob;        // a bug report on its way to the HomeNX Discord
    void openBugReport();                 // Settings > About > Make a bug report
    void makeAndSendReport(const std::string& note, bool send);
    void openSaveData(std::uint64_t only = 0); // Settings > Storage > Delete save data (only: one game's)
    void openGameInfo(const Game& g);     // + > About this game
    void openFirstSetup();                // after the intro: the choices worth making first
    std::uint64_t m_galleryApp = 0;       // the Album opened from a game's page: its pictures only
    void select(int index);
    void switchPage(int dir);     // the next / previous page in your order
    void goToPage(int page, int dir = 0);
    int pagePos(int page) const;  // where it is in your order
    bool pageHidden(int page) const; // Settings > Home > Pages to show
    int firstShownPage() const;
    void sortGames(int sort, bool keepChoice = true, bool requeue = true);
    int onlineOf(const Game& game) const; // Nextendo's players in it now (0: none or not known)
    bool isFavourite(std::uint64_t id);    // starred with + (first on the Games page)
    void toggleFavourite(const Game& game);
    void searchGames();                    // a name typed: the games with it, to jump to
    void jumpTo(int page, int index);      // that page, that game chosen
    void checkNextendoAlerts();            // people came online in a game he has: a notice
    void surpriseMe();                     // a game picked at random: chosen, to start with A
    void eraseCustomPage(int page);        // (its place in your order, hidden, the page shown: all kept right)
    void syncGenrePages();                 // Settings > Home > Genre pages: made, updated or taken away
    // Backups: sdmc:/config/CardsNX/backups/<date>/ (settings, genres, the game lists).
    bool backupNow(bool quiet);
    void autoBackup();                     // a week since the last: another
    void openBackups();                    // Settings > About > Backups
    void restoreBackup(const std::string& dir);
    void askGenres();                      // the Games page's games with no genre yet: to worker 0
    void selectById(std::uint64_t id); // its page and card, straight away
    std::uint64_t m_backTo = 0;        // back from this game: it stays chosen until you move
    struct BackStats {
        std::uint64_t id = 0;
        bool ok = false;
        PlayStatistics st;
        float ms = 0.f;
    };
    std::future<BackStats> m_backStatsJob; // m_backTo's play statistics, read at start off the screen's thread
    void takeBackStats();
    void takeLists(Lists lists);
    // The Media page: your media apps off the Homebrew page, and the ones
    // GitHub has that aren't on the SD card yet ("Get").
    void splitMedia();
    void moveToPage(const Game& g, int to); // + > Move to Games / Homebrew / Media
    std::vector<Game> homebrewToSave() const; // Homebrew and Media's .nro apps, for lists.json
    void getMedia(const Game& g);
    std::atomic<bool> m_updatesSaved{false}; // the last updates came from the saved list
    // Settings > Pictures > Save every picture now: every game's, on the SD card
    // for good (one taken down later stays), one game at a time on a thread.
    std::future<void> m_saveAllJob;
    std::atomic<int> m_saveAllDone{0}, m_saveAllTotal{0};
    std::atomic<bool> m_saveAllStop{false};
    // Set first thing in onDestroy: the background jobs give up quickly, so the
    // menu can close (for a game's launch, or HOME's hand-off) without waiting
    // out downloads and update checks — the daemon closes a menu that takes
    // too long, and the hand-off froze behind the wait.
    std::atomic<bool> m_jobsStop{false};
    void saveAllPictures(bool quiet = false);
    bool m_saveAllQuiet = false, m_autoSaveAsked = false;
    std::atomic<int> m_saveAllGot{0};
    std::future<bool> m_getJob;
    std::string m_getPath, m_getName;
    void openPageOrder(int select);
    void openPageShow();
    std::vector<std::string> m_sheetNames; // Settings > Home > Settings tab order, while it's open
    void openSheetOrder();
    void startListRefresh(bool gamesOnly = false); // read the lists again on a thread (unless that's running)
    float m_gamesChangedIn = 0.f; // the installed titles changed: read again once they stop (an update sends many)
    bool glassCard(const Game& g); // an app or a game without box art: its icon on glass
    void applyOrder(int page);
    void saveOrder(int page);
    void reloadGames();
    void invalidateGameCard(std::uint32_t error = 0);
    void updateGameCard(float dt);
    void drawGameCard(nxui::Renderer& renderer, float alpha);
    void clearGameCardPresentation();
    void refreshGameCardPresentation();
    std::unordered_set<std::uint64_t> movedToCustomPages() const; // off their own pages (shown custom pages only)
    std::vector<Game> m_moveSetAside; // Move on a built-in page: the list as it was, the moved-out games in it
    void openGameCardDisplay();
    void openGameCardIcons();
    std::vector<Game> m_gameCardPages[Settings::kMaxPages];
    std::uint64_t m_gameCardDetachedSel[Settings::kMaxPages]{};
    bool m_gameCardPinned = false;
    int m_gameCardEditDepth = 0;
    struct LibraryEdit {
        CardsActivity& owner;
        explicit LibraryEdit(CardsActivity& activity) : owner(activity) {
            owner.clearGameCardPresentation();
            ++owner.m_gameCardEditDepth;
        }
        ~LibraryEdit() {
            if (--owner.m_gameCardEditDepth == 0) owner.refreshGameCardPresentation();
        }
    };
    GameCardState m_gameCard;
    std::future<GameCardState> m_gameCardJob;
    std::unique_ptr<nxui::Texture> m_gameCardIcon;
    float m_gameCardIn = 0.f, m_gameCardCheckingFor = 0.f;
    std::uint64_t m_gameCardGeneration = 0, m_gameCardJobGeneration = 0;
    std::uint32_t m_gameCardMountError = 0;

    // Starting things.
    void play();
    void launchGame(const Game& g);
    void openNro(const std::string& path);
    void openNintendo(const Game& g);
    void openDiscord();
    void openBrowser();
    std::string findNro(const char* nameHas);
    // The launch: the card grows to fill the screen, the rest falls away, then
    // black; `then` runs at the end (index -1: no card, only the fade).
    void leave(int index, float seconds, std::function<void()> then);
    enum LaunchStyle { kFade, kSplash, kZoom, kFlip, kDive, kNoAnimation, kLaunchStyles };
    int launchStyle() const;
    bool launchDrawsCard() const; // the chosen card is drawn by drawLaunch (zoom, flip)
    float othersAlpha() const;
    float menuHide() const; // a menu over HOME: how far the games behind have gone (0..1), m_coverIn
    // Any menu over HOME (+, its lists, a picker, the details, a channel, Settings,
    // the bar's menus): the games and the panel go, and the top bar with them
    // unless the menu hangs from it. One pace for all, held while one menu hands
    // over to the next, so nothing shows through in between (no flicker).
    bool menuCovers() const;
    bool menuCoversBar() const;
    float m_coverIn = 0.f, m_coverIdle = 1.f; // the games: 0 shown .. 1 gone; seconds with no menu
    float m_barCoverIn = 0.f, m_barCoverIdle = 1.f; // the top bar, the same
    void comeBack(); // it didn't happen: the screen as it was
    void handleHome();
    void askClose(); // X: close the open game or app, after asking
    // The panel's right button: News, nothing, or a card (Settings.panelApp).
    const Game* panelCard() const;
    void openCard(const Game& g);
    void wantPanelIcon(const Game& g);
    // + > Rename (Settings.names): applied to the pages wherever they're made.
    void applyNames();
    void renameCard(const Game& g);
    AccountUidLike m_launchAs{}; // Settings > Home > Ask who's playing: the one chosen for this launch
    // Settings > Look > Background: one picture behind everything.
    struct Pixels {
        std::vector<std::uint8_t> rgba;
        int w = 0, h = 0;
    };
    std::unique_ptr<nxui::Texture> m_wallTex;
    std::future<Pixels> m_wallJob;
    void loadWallpaper();
    void openWallpapers(bool lockScreen = false); // lockScreen: the lock screen's picture instead
    static Pixels pictureFrom(const std::vector<std::uint8_t>& data, int maxW);
    static Pixels readPicture(const std::string& path, int maxW);
    static Pixels albumPixels(const AlbumShot& shot, int maxW);
    // Settings > Look > Lock screen picture: drawn sharp behind the clock
    // (shared with HomeNX's look: switchu/lock_picture.hpp).
    void loadLockPicture();
    std::string m_lockPicture, m_lockTexFor;
    std::unique_ptr<nxui::Texture> m_lockTex;
    std::uint64_t m_lockGameId = 0;
    std::string m_lockGameName;
    std::future<Pixels> m_lockJob;
    // Settings > Look > Lock screen: the time and date when the console wakes.
    bool m_tv1080 = false; // Settings > Look > TV resolution (config/SwitchU/resolution.txt)
    bool m_locked = false, m_lockFonts = false;
    float m_lockIn = 0.f, m_unlockT = -1.f; // opening it: 0 -> 1, then it's gone
    float m_lockIdle = 0.f;                 // locked and ignored for this long
    bool m_musicPausedForLock = false;      // the lock screen paused the music
    nxui::Font m_fLock, m_fLockDate;
    std::string m_date;
    void lock(bool shuffle = true); // (shuffle: a new wallpaper, if they rotate; not for a preview)
    void beginUnlock(); // the unlock animation starts; what the lock held comes back
    bool updateLock(float dt);
    void drawLock(nxui::Renderer& r);
#ifdef SWITCHU_MENU
    // The lock screen's password (the same as HomeNX's): D-pad presses, made on the lock screen.
    bool m_pwSet = false, m_pwSetup = false;
    switchu::lockpw::Entry m_pw;
    void startPasswordSetup();
#endif
    bool closeEverything();

    // Popups and screens.
    void openMenu(std::string title, std::vector<MenuItem> items);
    void closeMenu(); // m_menu cleared; what it showed fades out (the ghost)
    void openOptions();
    // The Music screen (the note in the bar, Music on the dots): a record that
    // turns, the song and its controls, the playlist, and the music's bars.
    bool m_musicOpen = false;
    float m_musicIn = 0.f, m_musicSpin = 0.f, m_musicBass = 0.f, m_musicListScroll = 0.f;
    int m_musicZone = 0, m_musicCol = 2, m_musicRow = 0; // zone 0 controls, 1 volume, 2 playlist
    bool m_musicVolDrag = false;
    std::array<float, Audio::kBands> m_viz{};          // the bars, eased
    void updateMusic();
    bool musicTouch();
    void drawMusic(nxui::Renderer& r);
    void musicControl(int col); // shuffle, previous, play / pause, next, repeat
    // Spotify in the music player (Settings > Sound > Music player; HOME only):
    // what plays on a phone, computer or speaker, read every few seconds while
    // the Music screen or Quick Settings shows it, and their buttons.
    bool spotifyOn() const;
    bool spotifySignedIn() const;
    bool homeMusic() const { return m_settings.music && !spotifyOn(); } // HOME's own songs play
    void spotifyDo(Spotify::Cmd c, int value = 0);
    float spotifyPos() const; // seconds into the song (it moves on from when it was read)
    void updateSpotify();
    Spotify::Now m_sp;
    nxui::Texture m_spCover;
    // Signing in: a QR code for a phone on the same Wi-Fi.
    void signInSpotify();
    void updateSpotifyQr();
    void closeSpotifyQr();
    void drawSpotifyQr(nxui::Renderer& r);
    bool m_spQrOpen = false;
    std::string m_spQrText;
    std::vector<std::uint8_t> m_spQr; // its squares, row by row
    int m_spQrSize = 0;
#ifdef SWITCHU_MENU
    Spotify m_spotify;
    bool m_spWatch = false;
    // Spotify through Switify (github.com/DomazinUS/Switify; the client is
    // cristowo/SwitchU's): its background service plays on the console, in
    // HOME and over games. m_sp is filled from it; the buttons go to it.
    switchu::switify::Client m_switify;
    std::string m_swCover;             // the cover shown (its URL)
    std::uint32_t m_swPos = 0;         // the last position it said (the song moves on from m_sp.at)
    bool m_swPlayWhenReady = false;    // started by Play: it plays once it's up
    float m_swStartedAt = 0.f;
    bool m_swPausedForSleep = false;
#endif
    // The Music screen's list with Spotify: your library, playlists, their songs.
    int spotifyRows() const;
    void spotifyRow(int row);  // A on it: a playlist opens, a song plays
    bool spotifyBack();        // B in the list: up a level (false at the top)
    // + > Title picture: the game's pictures to choose from, in a grid.
    bool m_pickOpen = false;
    Game m_pickGame;
    std::future<std::vector<TitleChoice>> m_pickJob;
    // A picker opened while the last one's search still ran: started when that one ends.
    std::function<std::vector<TitleChoice>()> m_pickNext;
    std::vector<TitleChoice> m_pickChoices;                 // (their pixels go once they're shown)
    std::vector<std::unique_ptr<nxui::Texture>> m_pickTex;
    int m_pickSel = 0;
    float m_pickIn = 0.f;
    std::future<bool> m_pickSave;
    std::uint64_t m_pickSaveId = 0;
    bool m_pickSaveTooBig = false; // the save failed on a picture HOME can't open
    bool m_pickCover = false, m_pickSaveCover = false; // the card's picture (not the title's)
    bool m_pickHero = false, m_pickSaveHero = false;   // the background's
    float m_pictureScale = 1.f; // 1.5 at 1080p: cards and icons are kept that much bigger
    // A background (kBackground, kLaunch: the screen; kTile: Channels' biggest tile) is kept
    // just big enough to cover that box at the screen's real pixels, never smaller: an
    // ultra-wide hero (1920 x 620) cut to 1280 wide was 413 tall and stretched 1.75x up
    // the 720 screen (the "not fully loaded" look). Only ever made smaller, never bigger.
    // At most 1.5x the box's pixels (a 3840 x 1240 one would be 14 MB docked): just under covering then.
    int coverW(int w, int h, bool tile) const {
        const bool big = m_settings.channelTiles == 1;
        const float bw = (tile ? (big ? 352.f : 268.f) : 1280.f) * m_pictureScale;
        const float bh = (tile ? (big ? 198.f : 151.f) : 720.f) * m_pictureScale;
        const float fw = static_cast<float>(std::max(1, w)), fh = static_cast<float>(std::max(1, h));
        float k = std::min(1.f, std::max(bw / fw, bh / fh));
        k = std::min(k, std::sqrt(1.5f * bw * bh / (fw * fh)));
        return std::max(1, static_cast<int>(std::ceil(fw * k)));
    }
    struct PlayStat {
        std::uint64_t id = 0, last = 0, seconds = 0;
        std::uint32_t launches = 0;
    };
    std::future<std::vector<PlayStat>> m_statsJob;
    bool m_statsAsked = false, m_statsEvery = false;
    std::atomic<bool> m_totalsFailed{false}; // the event log couldn't be counted (capped, stopped...): not tried again this boot
    AccountUidLike m_statsUser; // the profile m_statsJob was started for
    // Achievements (switchu/achievements.hpp): kept as the play data reaches them
    // (here from every game's stats, by the daemon during a game) and said on HOME.
    struct Achieved {
        std::vector<std::string> said; // "Name (Game)" for each new one
        long long newest = 0;          // the latest time anything was earned
    };
    static Achieved collectAchievements(const std::vector<PlayStat>& stats, const std::map<std::uint64_t, std::string>& names,
                                        bool every, long long seenAt, std::uint32_t installed);
    std::future<Achieved> m_achJob;
    void openAchievements(const Game& g);
    std::vector<MenuItem> overallAchievementRows();
    void welcomeBack(const Game& g);        // Achievements > Welcome back, as it starts
    bool m_scanAsked = false;              // Settings > Home > Look for new software
    bool m_autoClock = true;               // Settings > Console > Set the time automatically
    bool m_darkTheme = false;              // Settings > Console > Theme (Nintendo's screens)
    // The first time: a few pages on the basics.
    bool m_introOpen = false;
    int m_introPage = 0;
    float m_introIn = 0.f;
    void updateIntro(float dt);
    // Back from a game: its last picture, shrinking into its card as HOME appears.
    std::unique_ptr<nxui::Texture> m_returnTex;
    float m_returnT = 0.f;
    void drawReturn(nxui::Renderer& r);
    void drawIntro(nxui::Renderer& r);
    std::vector<std::uint64_t> m_scanBefore; // what HOME showed before it
    std::string m_pickQuery; // the Search's text ("" = the game's own name)
    int m_pickWall = 0;      // 1 Background, 2 the lock screen's picture, 3 the Album, 4 several to shuffle (else the game's pictures)
    std::string m_pickLabel; // the Search's game it shows (its query is "#id")
    std::future<std::vector<std::pair<long long, std::string>>> m_searchJob; // the Search's games
    std::string m_searchFor;
    int pickKind() const { return m_pickWall || m_pickHero ? 2 : m_pickCover ? 1 : 0; }
    void chooseWallpaper(const std::string& path, bool lockScreen);
    // The picture grids, a page at a time (mode: m_pickWall). The Album's opens one full screen.
    void openGallery(int mode = 3);
    void loadGalleryPicture();
    void deleteGalleryShot();
    void saveShuffle();
    void drawGalleryAsk(nxui::Renderer& r);
    bool m_galleryView = false;
    bool m_galleryAsk = false;   // "Delete this?" is up
    bool m_galleryStale = false; // one was deleted: the grid's page is read again
    int m_galleryAt = 0;
    int m_galleryPage = 0; // the grid's page (12 a page)
    std::string m_galleryCurrent; // the picture in use (marked on its tile)
    bool showGalleryPage(int page);
    static std::vector<TitleChoice> galleryPage(const std::vector<AlbumShot>& all, size_t from, const std::string& current);
    std::shared_ptr<std::vector<AlbumShot>> m_galleryAll; // all of them, newest first
    std::unique_ptr<nxui::Texture> m_galleryTex;
    std::future<Pixels> m_galleryJob;
    void openTitlePicker(const Game& g, bool cover = false, const std::string& query = {}, bool hero = false);
    void startPickJob(std::function<std::vector<TitleChoice>()> work);
    void searchPicker();
    // Text typed in: HomeNX's keyboard as HOME, the system's as an app.
    void askText(const std::string& title, const std::string& guide, const std::string& initial, int maxLength,
                 std::function<void(const std::string&)> done);  // (B / cancel: back where it was asked from, see m_backArm)
    void updatePicker();
    bool pickerTouch();
    void drawPicker(nxui::Renderer& r);
    void pickChoice(int index);
    // Plus: the card's art on the left, its name and details, big buttons.
    bool m_optOpen = false;
    int m_optSel = 0;
    float m_optIn = 0.f;
    float m_optLevelIn = 1.f; // a level change in the open sheet: re-staggers the buttons only
    float m_optOut = 0.f;     // 1 when the sheet closes -> 0 (it fades, taking no input)
    void closeOptions();      // m_optOpen = false, fading out
    Game m_optGame;
    std::vector<MenuItem> m_optItems;
    plus::Level m_optLevel = plus::Level::Top; // the level shown: the top, or a group opened in the same sheet
    plus::Card plusCard(const Game& g) const;   // the facts that choose its entries
    MenuItem plusItem(plus::Entry e, const Game& g); // one entry: its label, glyph and action
    void showOptionLevel(plus::Level level);     // fills the sheet with that level's entries
    std::vector<MenuItem> moveLevelItems(const Game& g); // Move to…'s entries
    bool libraryMovable(const Game& g) const;           // on Games / Homebrew / Media (not the Nintendo page)
    void moveCardTo(const Game& g, int from, int to);   // Move to… > a page (`from`: the page the sheet was opened on)
    void updateOptions();
    bool optionsTouch();
    void drawOptions(nxui::Renderer& r);
    void openSettings(int section = 0);
    // Cards hidden with + (nothing is deleted); Settings > Home > Hidden cards brings them back.
    void hideCard(const Game& g);
    void openHiddenCards();
    void dropHidden(std::vector<Game>& list) const;
    int m_tvResolution = 0;
    void buildSettings();
    void openPower();
    // The three dots: a menu dropping from its button, the pages on top and
    // tiles for everything else.
    struct DotsTile {
        const char* name;
        const char* glyph;
        nxui::Color tone;
        std::function<void()> run;
    };
    bool m_dotsOpen = false;
    int m_dotsRow = 1, m_dotsCol = 0;
    float m_dotsIn = 0.f;
    std::vector<DotsTile> m_dotsTiles;
    void openDots();
    void updateDots();
    bool dotsTouch();
    void drawDots(nxui::Renderer& r);
    void drawPower(nxui::Renderer& r);
    void updatePower();
    bool powerTouch();
    // Power: four big glass tiles (Sleep, Restart, Power Off, Hekate).
    bool m_powerOpen = false;
    int m_powerSel = 0;
    float m_powerIn = 0.f, m_powerLift[4] = {0.f, 0.f, 0.f, 0.f};
    std::vector<MenuItem> m_powerItems;
    std::vector<MenuItem> powerItems(); // Sleep, Restart, Shut down, Reboot to Hekate
    void openMusic();
    void openQuick();                   // R: Quick Settings, a glass panel on the right
    void drawQuick(nxui::Renderer& r);
    void updateQuick();
    bool quickTouch();
    void quickActivate(int row, int col);   // A on a button
    void quickChange(int row, int delta);   // left / right on a slider
    // Up / down, left / right, A and B on a list of rows; close() shuts it.
    void listInput(std::vector<MenuItem>& items, int& sel, const std::function<void()>& close,
                   const std::function<void()>& back = {});
    void openUsers();
    void openNotifications();
    void takeSwitchUOrder();
    void startMove();
    void finishMove(bool keep);
    void moveTo(int index);                    // the held card to this place (the others glide)
    void arrange(std::vector<Game> next);      // the page in a new order, its cards gliding there
    nxui::Vec2 slotPos(int index) const;       // a place's spot, as laid out now (no glide)
    void updateMove(float dt);
    void say(const std::string& message);
    void activateTop(int index);
    void activateBottom(int index);
    void press(int zone, int index);
    void keepTextCachesSmall();
    void setUser(AccountUidLike uid);
    void loadAvatar();
    std::string updateRowLabel();
    void runUpdate();

    void draw(nxui::Renderer& r);
    void drawBackground(nxui::Renderer& r);
    void drawTopBar(nxui::Renderer& r, float a);
    void drawCards(nxui::Renderer& r);
    void drawCardExtras(nxui::Renderer& r); // after the blur: glass cards, badges, the sheen
    void drawLaunch(nxui::Renderer& r);
    void drawPanel(nxui::Renderer& r, float a);
    void drawPopup(nxui::Renderer& r);
    void drawSheet(nxui::Renderer& r);
    void drawRow(nxui::Renderer& r, MenuItem& item, const nxui::Rect& row, bool chosen, float a, bool wide);
    // A menu's foot: each button in a glass pill, then what it does (right-aligned).
    void drawHints(nxui::Renderer& r, float right, float cy,
                   std::initializer_list<std::pair<const char*, const char*>> hints, float a);
    void drawHints(nxui::Renderer& r, float right, float cy, const std::vector<std::pair<const char*, const char*>>& hints, float a);
    void glassCircle(nxui::Renderer& r, float cx, float cy, float d, bool focused, const char* icon, float iconSize,
                     float a, float pressed);
    void frost(nxui::Renderer& r, const nxui::Rect& rect, float radius, const nxui::Color& tint);
    void icon(nxui::Renderer& r, const char* name, float cx, float cy, float size, const nxui::Color& tint);
    bool cardRect(int index, nxui::Rect& card, float& chosen); // with a move's glide and jiggle
    bool cardRectAt(int index, nxui::Rect& card, float& chosen);
    bool rowShows(int index) const;
    const nxui::Texture* gameIconTexture();
    nxui::Color accent(float a = 1.f) const;
    nxui::Color onAccent(float a = 1.f) const; // text and icons drawn on the accent colour
    nxui::Color glassTint(float a = 1.f) const;
    bool isRunning(std::uint64_t id) const;
    std::uint64_t runningId() const;

    bool updateSheet(float dt);
    bool updatePopup();
    bool handleTouch();

    Settings m_settings;
    Audio m_audio;
    AccountUidLike m_user;

    nxui::Font m_fTitle, m_fPlay, m_fLabel, m_fPill, m_fBody, m_fBadge, m_fCount;
    std::map<std::string, std::unique_ptr<nxui::Texture>> m_icons; // each icon loads the first time it's drawn (icon())
    // The buttons' icons: their small files read into RAM once, off the screen's
    // thread (an SD read at each icon's first draw stuttered the buttons; the
    // files are ~3 KB, all of them together one card transaction).
    struct IconPixels {
        std::string name;
        std::vector<std::uint8_t> rgba;
        int w = 0, h = 0;
    };
    std::map<std::string, IconPixels> m_iconFiles; // decoded, waiting for their first draw
    std::future<std::vector<IconPixels>> m_iconFilesJob;
    // Every game's cover as a very small picture: read and decoded for all of
    // them at once while HOME starts, uploaded a few a frame, drawn until the
    // full cover arrives (a card never shows its name instead of its cover).
    struct Thumb {
        std::unique_ptr<nxui::Texture> tex;
        float in = 0.f;
        ArtKind kind = ArtKind::Grid; // (a box cover's red band is trimmed, as the full one's)
    };
    struct ThumbPixels {
        std::uint64_t id = 0;
        std::vector<std::uint8_t> rgba;
        int w = 0, h = 0;
        ArtKind kind = ArtKind::Grid;
    };
    std::map<std::uint64_t, Thumb> m_thumbs;
    std::vector<ThumbPixels> m_thumbUploads;
    std::future<std::vector<ThumbPixels>> m_thumbsJob;
    std::shared_ptr<nxui::Widget> m_painter;
    bool m_frosted = false;
    // The glass's blurred copy of the screen: made again only when what's behind
    // it changes (every few frames while the background drifts). The GPU was the
    // limit when it ran slow (a game open behind HOME, a low clock): frames took
    // 66 ms. If it still can't keep up, the glass goes plain for the session.
    int m_blurAge = 1000;
    float m_gpuWaitAvg = 0.f;
    bool m_gpuSlow = false;
    // The longest part of the last draw (the slow-frame lines).
    std::uint64_t m_drawMarkTick = 0;
    float m_drawMarkMs = 0.f;
    const char* m_drawMarkName = "";
    void drawStep(const char* next);

    std::vector<Game> m_pages[Settings::kMaxPages];
    int m_sels[Settings::kMaxPages] = {};
    int m_page = kGames;
    std::map<std::uint64_t, Art> m_art;
    float m_scroll = 0.f;  // the row's position, easing to sel()
    float m_repeat = 0.f;  // held left / right
    float m_wheelRepeat = 0.f; // held up / down (the wheel)
    Zone m_zone = Zone::Cards;
    int m_topSel = 0, m_bottomSel = 0;

    // Motion: everything eases on m_time.
    float m_time = 0.f;
    // The decorations' clock (focus ring's pulse, the background's drift, the
    // music's little meter, the players' dot...): stops in deep idle, so they
    // stand still and pick up where they were, without a jump.
    float m_decoTime = 0.f;
    float m_pictureInAt = -1000.f; // a picture arrived (it fades in): drawn at full rate a moment
    void pictureArrived();
    float m_intro = 0.f;          // 0 -> 1 as it opens
    float m_introFor = 1.1f;      // seconds (back from a game: quicker)
    float m_retryIn = 5.f;        // pictures that didn't come are asked again
    // How far from the selection covers are asked for. It widens on the retry
    // after HOME is up, so coming back from a game doesn't read the whole
    // shelf at once; browsing widens it at once.
    int m_artRadius = 1;
    float m_artArrivedAt = -10.f; // the last picture shown (fading in for a moment)
    float m_evictWait = 0.f; // a picture let go: its memory frees 4 frames on (nothing else goes until then)
    std::future<Lists> m_listJob;
    float m_pageIn = 1.f;         // 0 -> 1 after a page switch
    int m_pageDir = 1;
    float m_detailIn = 1.f;       // 0 -> 1 when the chosen game changes (its details fade in)
    float m_sheen = 1.f;          // 0 -> 1: light across the chosen card
    float m_menuIn = 0.f;         // the popup opening
    int m_pressZone = -1, m_pressIndex = -1;
    float m_pressT = 1.f;         // 0 -> 1 after a button is pressed

    // The launch (see leave()).
    bool m_leaving = false;
    float m_leaveT = 0.f, m_leaveFor = 0.9f;
    int m_leaveIndex = -1;
    std::uint64_t m_leaveId = 0;
    nxui::Rect m_leaveFrom{};
    std::function<void()> m_leaveThen;
    bool m_left = false; // `then` ran: hold the black frame until the app closes
    float m_leftAt = 0.f; // when (12 s on, it's given up)
    std::unique_ptr<nxui::Texture> m_launchTex; // the game's art at full size
    std::uint64_t m_launchTexId = 0;
    float m_launchTexIn = 0.f;

    // Moving a card (Plus > Rearrange, or a touch held 0.55 s), as SwitchU's edit
    // mode: the held one rides the cursor, lifted and pulsing; the others jiggle
    // and glide to where they'd go (the grid swaps, as SwitchU's grid drops; the
    // row inserts, as its single row). A puts it down, B puts it all back.
    bool m_moving = false;
    std::vector<Game> m_moveBefore;
    int m_moveBeforeSel = 0;
    std::uint64_t m_moveId = 0;
    float m_moveJiggle = 0.f, m_moveTime = 0.f; // the others' jiggle (0..1, 0.18 s), its phase
    bool m_moveTouch = false;                   // dragged with a finger: the held tile follows it
    float m_moveTouchX = 0.f, m_moveTouchY = 0.f;
    int m_moveTouchFrom = 0, m_moveEdgeDir = 0;
    float m_moveEdgeHold = 0.f;
    float m_yAt = 0.f; bool m_yArmed = false; // Y: tapped (the grid) or held (Move)
    int m_holdIndex = -1; // the card under a touch that began on one (held 0.55 s: picked up)
    struct Glide {
        float dx = 0.f, dy = 0.f, t = 0.f, dur = 0.2f;
    };
    std::unordered_map<std::uint64_t, Glide> m_glides; // cards on their way to a new place (by id)

    // The GPU's time per drawing step (Settings > perf log): timestamps between
    // the steps, read 3 frames on (done by then: NUM_FB is 2), summed per 10 s.
    static constexpr int kGpuRing = 4, kGpuMarks = 24;
#ifdef NXUI_BACKEND_DEKO3D
    dk::UniqueMemBlock m_gpuTimes;
#endif
    std::uint64_t m_gpuFrame = 0;
    int m_gpuMark = 0;
    const char* m_gpuNames[kGpuRing][kGpuMarks] = {};
    bool m_gpuMenu[kGpuRing] = {}, m_gpuBlur[kGpuRing] = {};
    std::map<std::string, std::pair<float, float>> m_gpuSteps; // sum, max (ms)
    std::map<std::string, int> m_gpuOverBy;
    int m_gpuFrames = 0, m_gpuOver = 0, m_gpuOverMenu = 0, m_gpuOverBlur = 0, m_gpuBlurs = 0, m_gpuMenus = 0;
    float m_gpuTotalSum = 0.f, m_gpuTotalMax = 0.f, m_gpuWindowAt = 0.f;
    void gpuFrameBegin();
    void gpuMark(const char* ended);

    // The background: the chosen game's art, faded in over the last one.
    std::uint64_t m_bgId = 0, m_bgPrevId = 0;
    // The background on screen: always a real one (the game's own, loaded).
    // The newly chosen game's comes in over it once it's here; until then the
    // last one stays (its card stretched, or the card's small copy, stood in
    // before: a blurry picture, then the right one, at each step scrolling).
    std::uint64_t m_bgShowId = 0, m_bgFromId = 0;
    float m_bgShowAt = -10.f;
    std::uint64_t m_bgWantId = 0; // (the chosen game since m_bgWantAt: one with no background at all lets the old one go after 2 s)
    float m_bgWantAt = 0.f;
    float m_bgFade = 1.f;

    // The status bar (numbers from worker 1: the SD card and network calls
    // took the screen's time once a second).
    std::unique_ptr<nxui::Texture> m_avatar;
    std::string m_clock, m_battery, m_free;
    bool m_online = false, m_charging = false;
    std::future<Pixels> m_avatarJob; // the picture of you, read on a thread (loadAvatar)
    struct Status {
        std::string clock, battery, free;
        bool online = false, charging = false;
        std::string date; // "Friday 31 October" (the lock screen)
        int temperature = -1; // the chip's, C (every 5 s)
    };
    Status m_statusIn;
    int m_temp = -1;
    bool m_hotSaid = false; // "the Switch is hot" was said (again once it cooled)
    // Settings > Console > Test the SD card.
    std::future<std::string> m_sdTestJob;
    std::atomic<int> m_sdTestPct{0};
    std::string m_sdTestResult;
    void drawMessage(nxui::Renderer& r);
    // A whole panel over Settings for a job that takes a while: updating, USB file
    // transfer, the SD card test, pairing a controller.
    enum class Task { None, Update, Mtp, SdTest, Pairing };
    struct TaskView {
        std::string title, status, detail, button; // button: what A does ("" = nothing)
        const char* icon = "gear";
        float progress = -1.f; // 0..1, -1 = no bar
    };
    Task m_task = Task::None;
    float m_taskIn = 1.f;
    std::string m_updateInstalled; // the build installed now (a file: read once)
    void openTask(Task t);
    void closeTask();
    void flipMtp();
    TaskView taskView();
    void taskButton();
    void updateTask();
    void drawTask(nxui::Renderer& r);
    void startSdTest();
    bool m_statusFresh = false;
    int m_unread = 0;
    std::map<std::uint64_t, int> m_players; // Nextendo, by title
    std::vector<NextendoGame> m_nextendoGames; // one row per game (the list)
    int m_playersTotal = -1;                 // -1: not known yet
    nxui::Rect m_userHit{}, m_playersHit{};  // in the pill, for touch
    std::future<bool> m_browserJob;          // BrowseNX downloading

    // Homebrew updates, found as the Homebrew Manager finds them (GitHub
    // releases, the HB App Store) and remembered for a few hours in
    // sdmc:/config/CardsNX/updates.json. By .nro path; only newer versions.
    struct NroUpdate {
        std::string latest, source, url, zipEntry;
        bool zip = false;
    };
    std::map<std::string, NroUpdate> m_updates;
    std::future<std::map<std::string, NroUpdate>> m_updatesJob;
    std::future<std::string> m_nroUpdateJob;   // one being installed: "" done, else what went wrong
    std::string m_nroUpdating;                 // its path
    std::vector<std::string> m_nroQueue;       // Update all: the ones after it
    std::atomic<float> m_nroProgress{0.f};
    bool m_updatesChecked = false;
    void checkHomebrewUpdates(bool force);
    void updateNro(const std::string& path);
    const NroUpdate* nroUpdate(const Game& g) const;
    void openHomebrewUpdates();
    std::future<std::uint32_t> m_deleteJob;  // an NRO forwarder being uninstalled (its Result)
    std::uint64_t m_deleteId = 0;
    std::string m_deleteName;

    // Popups: a list (menu, power, music, users, a game's options), or the notifications.
    std::string m_menuTitle;
    std::function<void()> m_menuBack; // B: back to the list before (set after openMenu), else closed
    // B goes back one step, not out: while a + sheet button or a list's row runs,
    // m_backArm reopens what it was run from (the sheet at its level, or the list
    // as it was). Whatever that run opens takes it as its way back: a list (its
    // m_menuBack), the picker (m_pickBack), the details panel (m_crossBack), the
    // keyboard (its cancel). Unclaimed, it's dropped when the run ends.
    std::function<void()> m_backArm;
    bool m_backArmList = false;          // the arm reopens a list (title m_backArmTitle)
    std::string m_backArmTitle;
    std::function<void()> m_backArmParent; // that list's own way back (a list that opens itself again keeps it)
    int m_backArmSel = 0;
    std::function<void()> m_pickBack, m_crossBack;
    void armBackToList(const MenuItem& row); // m_backArm: the open list again, as it is now (not for Back / Cancel rows)
    void endBackArm();    // after a list row's run: a way back to a list from before a change is dropped
    bool m_backClaimed = false; // a list took the list arm as its way back
    unsigned m_backArmSaves = 0;
    void armBackToOptions(); // m_backArm: the + sheet again, at its level and button
    std::function<void()> takeBackArm() { m_backArmList = false; m_backArmParent = nullptr; return std::exchange(m_backArm, nullptr); }
    void reopenOptions(const Game& g, plus::Level level, int sel);
    std::vector<MenuItem> m_menu;
    int m_menuSel = 0;
    float m_menuOut = 0.f;              // 1 when the list closes -> 0: its ghost fades (no input)
    std::vector<MenuItem> m_menuGhost;  // what the closing list draws
    std::string m_menuGhostTitle;
    bool m_menuKeepDim = false;         // opened from the + sheet: the backdrop stays dimmed (no flash)
    bool m_notifOpen = false;
    std::vector<std::pair<std::string, std::string>> m_notifRows; // title, detail
    std::string m_message;
    struct Notice { std::string title, text, glyph; };
    std::deque<Notice> m_notices;
    Notice m_notice;
    float m_noticeAge = 0.f;
    int m_lastBatteryNotice = 101;
    switchu::lock_screen::ControllerNoticeGate m_controllerNotices;
    bool m_wakeStart = false;
    unsigned m_wakePresentedFrames = 0;
    bool m_asleep = false;
    float m_controllerCheckIn = 0.f;
    void updateNotices(float dt);
    std::future<std::uint32_t> m_storageJob;
    std::string m_storageGame;
    std::array<std::string, 2> m_storageSummary = {"Reading...", "Reading..."};
    struct StorageSnapshot {
        std::array<std::string, 2> summary;
        std::map<std::uint64_t, std::string> locations;
    };
    std::map<std::uint64_t, std::string> m_storageLocations;
    std::future<StorageSnapshot> m_storageReadJob;
    float m_messageFor = 0.f;
    std::vector<std::unique_ptr<nxui::Texture>> m_userPictures;

    // Quick Settings (R), laid out as HomeNX's: the status, the music player,
    // three sliders, two switches, the power buttons; the playlist beside it.
    enum QuickRow { kQPlayer, kQMusic, kQSounds, kQBrightness, kQAirplane, kQWifi, kQBluetooth, kQPower, kQRows };
    bool m_quickOpen = false, m_quickPlaylist = false;
    float m_quickIn = 0.f, m_quickRefreshIn = 0.f, m_quickListIn = 0.f;
    int m_quickRow = kQPlayer, m_quickCol = 1, m_quickListSel = 0;
    std::vector<MenuItem> m_quickPower;  // Sleep, Restart, Shut down, Reboot to Hekate
    QuickStatus m_quick;
    // Settings > Internet, Controllers, Console.
    ConsoleSettings m_console;
    int m_nextendoMode = 0;  // 1: Nextendo mode is on (Settings > Nextendo)
    // What Settings and Quick Settings show of the console, read on a thread:
    // on the screen's thread, opening Settings froze for 2-5 s while HOME's
    // start-up kept the system busy (perf.log, build 173).
    struct Facts {
        ConsoleSettings console;
        int nextendo = 0;
        QuickStatus quick;
    };
    std::future<Facts> m_factsJob;
    void readFactsLater();
    void takeFacts(bool wait); // wait: a row is about to change them, so the real ones first
    void openNextendo(int mode); // Prelude, Nextendo's app, which switches it (-1: just open it)
    void openPlayerList();       // every game Nextendo supports, and who is on it now
    std::future<bool> m_nxGetJob; // Prelude being downloaded (it wasn't on the SD card)
    int m_nxGetMode = -1;
    std::shared_ptr<NetTest> m_netTest;
    std::future<void> m_netTestJob, m_findJob;
    void startConnectionTest();
    std::string netTestText(int row); // 0 the test, 1 ping, 2 download, 3 upload, 4 signal
    void findControllersNow();
    void flipBluetooth(); // on / off (Quick Settings and Settings > Bluetooth)
    // Settings > Updates > Update log: what changed, newest first (CI writes updates.txt).
    void openUpdateLog();
    void updateLog();
    void drawLog(nxui::Renderer& r);
    bool m_logOpen = false;
    float m_logIn = 0.f, m_logScroll = 0.f, m_logEnd = 0.f;
    struct LogEntry {
        std::string date;
        std::vector<std::string> lines;
    };
    std::vector<LogEntry> m_log;
    // Bluetooth headphones and speakers (13.0.0+), as Nintendo's System Settings has them.
    bool bluetoothAudio(); // btm:sys open (btmsysExit after), else it says why not
    void startAudioScan();
    void updateBluetoothAudio(float dt);
    void refreshAudioName();
    void openAudioDevices();
    void connectAudio(const std::array<std::uint8_t, 6>& address, const std::string& name);
    void stopAudioScan();
    void showAudioScan();
    bool m_btScanning = false;
    float m_btScanLeft = 0.f, m_btScanPoll = 0.f, m_btConnectIn = 0.f, m_btConnectPoll = 0.f;
    std::vector<std::pair<std::array<std::uint8_t, 6>, std::string>> m_btFound; // what a search has found so far
    std::array<std::uint8_t, 6> m_btConnectAddr{};
    std::string m_btConnectName, m_btAudioName; // the one being connected; the one connected
    Mtp m_mtp; // Settings > Console > Connect to a computer: USB file transfer from HOME itself
    std::uint64_t m_mtpWaitingTick = 0; // since when it has waited for a computer (0: it isn't waiting)
    std::uint64_t m_mtpSeenTick = 0;    // the frame updateMtp() last ran in
    void updateMtp();
    Mtp::State m_mtpShown = Mtp::State::Off; // its state when last drawn (a change is drawn even in deep idle)


    // Settings: sections on the left, their rows on the right.
    std::vector<Section> m_sheet;
    bool m_sheetOpen = false, m_sheetOnRows = false;
    int m_sheetSec = 0, m_sheetRow = 0;
    float m_sheetIn = 0.f, m_sheetRowsIn = 1.f, m_sheetScroll = 0.f;

    // The workers: pictures (three at a time), Nextendo's counts, the status bar.
    std::vector<std::thread> m_threads;
    std::mutex m_mu;
    std::condition_variable m_cv;
    std::deque<Job> m_jobs;
    std::deque<Job> m_idleJobs; // the grid's other icons: taken when nothing else is wanted
    std::vector<Done> m_done;
    std::map<std::uint64_t, int> m_playersIn;
    std::vector<NextendoGame> m_nextendoIn;
    bool m_playersFresh = false;
    // Genres (the Genre sort): the screen's copy; the new ones from worker 0;
    // the games it should ask nlib.cc about (all three under m_mu but the first).
    std::map<std::uint64_t, std::string> m_genres;
    std::map<std::uint64_t, std::string> m_genresIn;
    bool m_genresFresh = false;
    std::vector<std::uint64_t> m_genreWant;
    std::unordered_set<std::uint64_t> m_genreAsked; // (the screen's: each game asked once a start)
    bool m_playersLive = false; // m_playersIn came from the site (not the SD card's copy): alerts
    std::unordered_set<std::uint64_t> m_favIds;
    bool m_favDirty = true;
    std::map<std::uint64_t, int> m_alertPrev;       // the last live counts, his games (none: no baseline yet)
    bool m_alertBaseline = false;
    std::map<std::uint64_t, float> m_alertAt;        // when each game was last told (once in 30 minutes)
    bool m_quit = false;
    // The console sleeps (set under m_mu, then m_cv told): the workers stop
    // reading the bar's state and asking Nextendo, and only wait for jobs.
    std::atomic<bool> m_workersAsleep{false};
    unsigned m_wakeGen = 0;                 // (under m_mu) +1 at each wake: the bar is read again at once
    std::atomic<bool> m_statusNow{false};   // the bar's numbers to read again at once (the clock was changed)
    std::atomic<bool> m_deepIdleNow{false}; // the deep idle tier (read by the workers)
    std::atomic<bool> m_wantPlayers{true};  // Nextendo's counts are shown (Settings > players)
    std::string m_key;        // SteamGridDB
    std::atomic<bool> m_netUp{false};    // worker 0 starts the network
    std::atomic<bool> m_netReady{false}; // ...the other workers wait for that first
    int m_startStage = 0;                // what the first frame doesn't need starts after it

    // The performance log (sdmc:/config/CardsNX/perf.log): how long starting
    // took, slow frames and what they were doing, pictures that took long.
    void perfLine(const std::string& line);
    float sinceStartMs() const;
    std::string m_perfOut; // waiting for worker 1 to write it (under m_mu)
    std::vector<std::array<std::string, 3>> m_notifyOut; // notifications for worker 1 to save (under m_mu)
    std::uint64_t m_perfCreateTick = 0, m_perfLastTick = 0;
    float m_perfPumpMs = 0.f, m_perfArtMs = 0.f, m_perfDiscordMs = 0.f, m_perfUpdateMs = 0.f, m_perfDrawMs = 0.f;
    int m_perfUploads = 0, m_perfFrames = 0, m_perfSlow = 0, m_perfVerySlow = 0;
    // The longest step of the last update (perfMark between steps), and the
    // GPU waits and picture frees nxui counted.
    std::uint64_t m_perfMarkTick = 0;
    float m_perfMarkMs = 0.f;
    const char* m_perfMarkName = "";
    void perfMark(const char* step);
    // Every 50 ms: when the screen's thread stays in one step for long, the log
    // says which; when this thread itself didn't run for a second, the whole
    // process was stopped (the system paused HOME), not CardsNX.
    void watchdog();
    float m_perfWindow = 0.f, m_perfWorst = 0.f;
    std::uint64_t m_perfShownAt = 0; // app().presentedFrames() at the window's start
    bool m_perfFirstCard = false;
    std::atomic<bool> m_boxArt{true};
    std::atomic<bool> m_titleArt{true}; // Settings > Look > Title pictures
    std::atomic<bool> m_clock24{true};
    std::atomic<bool> m_downloading{false}; // BrowseNX
    float m_evictIn = 0.f;
    bool m_evictNow = false; // a picture had no room: far ones go now
    bool m_shortBefore = false; // memory was short at the last letting go too (covers go only then)
    nxui::Theme m_theme = nxui::Theme::dark();
    void applyTheme(); // CardsNX's glass, white text and highlight colour, for HomeNX's screens in it

    // Touch: drag the row, tap a card (again to play), tap any button.
    bool m_dragging = false, m_touchOnCards = false;
    float m_touchScroll = 0.f;
    // Swipes with momentum: where the finger was over the last ~100 ms (time,
    // x) gives its speed when it lets go; the row glides on (m_flingV, cards a
    // second) and slows to a stop on a card; a grid flick turns 1-3 pages.
    std::deque<std::pair<float, float>> m_touchTrail;
    float m_flingV = 0.f;
    float touchSpeed() const; // pixels a second, left negative

#ifdef SWITCHU_MENU
    // As HOME.
    std::uint64_t m_startSuspended = 0;
    bool m_startRunning = false;
    bool m_atBoot = true;
    bool m_fromApplet = false; // back from one of Nintendo's screens (not from a game)
    bool m_startedOk = false;  // it ran a moment: not a start that crashes (kCardsStarting)
    int m_reopenSec = -1, m_reopenRow = 0; // Settings comes back where it was (rememberSheet)
    void rememberSheet();
    AppletLauncher m_launcher;
    std::uint64_t m_pendingLaunch = 0; // starts once the open game has closed (a game's id, or a .nro's)
    void resumeOpen(int index);
    void askCloseFirst(std::uint64_t open, const std::string& title, std::uint64_t next);
    void syncHomebrewFlag(); // Settings > Home > Keep homebrew open, for the daemon
    void pumpDaemon();
    std::uint64_t hostTitleId() const;
    AccountUid launchUid() const;

    // HomeNX's Discord and its keyboard, in HOME's font (loaded when first needed).
    nxui::Font m_hxFont, m_hxSmall;
    bool m_hxFonts = false;
    void ensureHxFonts();
    DiscordService m_discord;
    std::uint64_t m_unreadDmsVersion = ~std::uint64_t{0}; // m_discord.version() m_unreadDms was counted at
    int m_unreadDms = 0;
    std::shared_ptr<DiscordPanel> m_discordPanel;
    std::shared_ptr<TextEntryScreen> m_textEntry;
    std::shared_ptr<SocialScreen> m_social;   // Friends, your profile, Mii Maker (HomeNX's, not Nintendo's)
    nxui::Font m_socFont, m_socSmall;         // CardsNX's typeface at the sizes HomeNX's screens are laid out for
    void ensureTextEntry();
    void enterText(const std::string& title, const std::string& guide, const std::string& initial, int maxLength,
                   std::function<void(const std::string&)> done, nxui::Widget* back, bool sensitive = false,
                   std::function<void()> cancelled = nullptr);
    bool m_discordTokenEntryPending = false; // Discord's panel asked for the token keyboard
    void openSocial();
    void syncPopupsFlag();
    MenuItem friendCodeRow();
    std::future<std::string> m_friendCodeJob; // the user's friend code, for their popup
    std::string m_friendCode;
    std::shared_ptr<ControllerTestScreen> m_padTest; // Settings > Controllers: HomeNX's test screen
    void openPadTest();
    bool m_pairing = false;                          // HomeNX's own pairing, 30 s
    std::uint8_t m_pairedBefore = 0;
    int m_padsBefore = 0;
    float m_pairingLeft = 0.f, m_pairingPoll = 0.f;
    void startPairing();
    void stopPairing();
    void updatePairing(float dt);
    float m_discordStartIn = 10.f;
    bool m_discordStarted = false;

    // News, in CardsNX's glass: the list on the left, the article on the right.
    struct NewsItem {
        std::string id, title, topic, body, footer;
        bool unread = false;
        std::shared_ptr<std::vector<std::uint8_t>> picture; // JPEG
        std::vector<std::string> titleLines, lines;         // wrapped (when first shown)
    };
    void openNews();
    void showNewsItem(int index);
    bool updateNews(float dt);
    void drawNews(nxui::Renderer& r);
    std::future<void> m_newsJob;
    std::vector<NewsItem> m_newsReady, m_newsItems;
    std::string m_newsEmpty;
    bool m_newsWanted = false, m_newsOpen = false;
    int m_newsSel = 0;
    float m_newsIn = 0.f, m_newsListScroll = 0.f, m_newsScroll = 0.f, m_newsTouchY = 0.f, m_newsTouchScroll = 0.f;
    bool m_newsDragging = false;
    std::map<int, std::unique_ptr<nxui::Texture>> m_newsPictures; // a few, by index
    float m_newsLoadedAt = -1000.f;

    // Settings > Updates: HomeNX's release, installed from here.
    enum class Update { Idle, Checking, UpToDate, Available, Installing, Installed, Failed };
    std::atomic<int> m_update{static_cast<int>(Update::Idle)};
    std::future<void> m_updateJob;
    switchu::manager::ReleaseInfo m_release;
    std::string m_updateError;
    std::atomic<float> m_updateDownload{0.f}, m_updateInstall{0.f};
    std::atomic<int> m_updateStage{0};
#endif
};

} // namespace cards
