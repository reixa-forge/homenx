#pragma once
// The home screen's layouts (Y cycles them; Settings > Look > Home layout) and
// the geometry of the three that share the card row's model: Deck and Wheel
// (the row's selection and scroll, laid out differently) and Channels (a grid
// style). Pure (no UI): CardsActivity draws what these place.

#include "Settings.hpp"

#include <algorithm>
#include <cmath>

namespace cards::layout {

enum class Layout : int { Cards, Deck, Wheel, Grid, Channels, Cross, Count };

constexpr int kRowCards = 0, kRowDeck = 1, kRowWheel = 2, kRowStyles = 3;
constexpr int kGridIcons = 0, kGridChannels = 1, kGridStyles = 2;

inline Layout current(const Settings& s) {
    if (s.xmbView) return Layout::Cross;
    if (s.gridView) return s.gridStyle == kGridChannels ? Layout::Channels : Layout::Grid;
    return s.rowStyle == kRowDeck ? Layout::Deck : s.rowStyle == kRowWheel ? Layout::Wheel : Layout::Cards;
}

// Only the fields that say which layout it is change (the grid's size, the
// cross view's style and the rest stay as they were).
inline void apply(Settings& s, Layout l) {
    switch (l) {
    case Layout::Cross:
        s.xmbView = true;
        s.gridView = false;
        return;
    case Layout::Grid:
    case Layout::Channels:
        s.xmbView = false;
        s.gridView = true;
        s.gridStyle = l == Layout::Channels ? kGridChannels : kGridIcons;
        return;
    default:
        s.xmbView = false;
        s.gridView = false;
        s.rowStyle = l == Layout::Deck ? kRowDeck : l == Layout::Wheel ? kRowWheel : kRowCards;
        return;
    }
}

// Y: Cards, Deck, Wheel, Grid, Channels, Cross, and round again.
inline Layout next(Layout l, int step = 1) {
    constexpr int n = static_cast<int>(Layout::Count);
    return static_cast<Layout>(((static_cast<int>(l) + step) % n + n) % n);
}

inline const char* name(Layout l) {
    switch (l) {
    case Layout::Deck: return "Deck";
    case Layout::Wheel: return "Wheel";
    case Layout::Grid: return "Grid";
    case Layout::Channels: return "Channels";
    case Layout::Cross: return "Cross view";
    default: return "Cards";
    }
}

inline bool rowModel(Layout l) { return l == Layout::Cards || l == Layout::Deck || l == Layout::Wheel; }
inline bool gridModel(Layout l) { return l == Layout::Grid || l == Layout::Channels; }

// ---------------------------------------------------------------- Deck
// A handheld-console library style: the chosen game's art fills the screen
// (the background), its name and details sit over a row of portrait capsules
// along the bottom, and the chosen capsule is the first in the row, larger;
// the ones before it go off to the left. rel: the capsule's index minus the
// row's scroll (fractional while it glides).
namespace deck {
constexpr float kLeft = 80.f;                 // the chosen capsule's left edge
constexpr float kW = 176.f, kH = 250.f;       // a capsule
constexpr float kGrow = 1.14f;                // the chosen one
constexpr float kPitch = 196.f;               // one capsule to the next (20 apart)
constexpr float kBottom = 650.f;              // the row's bottom edge
constexpr float kTitleY = 300.f, kInfoY = 334.f; // the chosen game's name and details, over the row
}

struct DeckSlot {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    float chosen = 0.f; // 1 the chosen one .. 0 the rest
    bool shown = false;
};

inline DeckSlot deckSlot(float rel) {
    using namespace deck;
    DeckSlot s;
    s.chosen = std::clamp(1.f - std::abs(rel), 0.f, 1.f);
    const float grow = 1.f + (kGrow - 1.f) * s.chosen;
    s.w = kW * grow;
    s.h = kH * grow;
    // The ones after the chosen one make room for its growth (20 px apart either side).
    s.x = kLeft + rel * kPitch + kW * (kGrow - 1.f) * std::clamp(rel, 0.f, 1.f);
    s.y = kBottom - s.h;
    s.shown = s.x < 1290.f && s.x + s.w > -10.f;
    return s;
}

// ---------------------------------------------------------------- Wheel
// The page's games on a gentle arc down the right side, the chosen one in the
// middle; smaller and fainter toward the ends.
namespace wheel {
constexpr float kMidY = 380.f, kPitch = 76.f;
constexpr float kX = 680.f, kBend = 14.f;  // the chosen row's left edge; how far rows bend right per step squared
constexpr float kW = 470.f, kH = 66.f;
constexpr float kEnds = 4.f, kShown = 4.5f; // full fall-off at 4 steps; gone by 4.5
constexpr int kJump = 8;                    // L / R: rows a jump
// The chosen game's cover on the left, its name and when it was played under it.
constexpr float kCoverX = 110.f, kCoverY = 118.f, kCoverW = 300.f, kCoverH = 425.f;
}

struct WheelSlot {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f; // the row's rect
    float scale = 1.f, alpha = 1.f;
    bool shown = false;
};

inline WheelSlot wheelSlot(float rel) {
    using namespace wheel;
    WheelSlot s;
    const float a = std::abs(rel);
    s.shown = a < kShown;
    const float k = std::min(a, kEnds) / kEnds;
    s.scale = 1.f - 0.3f * k;
    s.alpha = (1.f - 0.75f * k) * std::clamp((kShown - a) / (kShown - kEnds), 0.f, 1.f);
    s.w = kW * s.scale;
    s.h = kH * s.scale;
    s.x = kX + kBend * rel * rel;
    s.y = kMidY + rel * kPitch - s.h * 0.5f;
    return s;
}

// ---------------------------------------------------------------- Channels
// A grid style: 4 x 3 wide tiles a page.
struct GridCell { int cols, rows; float w, h, padX, padY; };
constexpr GridCell kChannelCell{4, 3, 268.f, 151.f, 22.f, 20.f};
constexpr float kChannelRadius = 22.f;
constexpr float kChannelFocusScale = 1.06f;

// The middle 16:9 of a w x h picture, as a source rect {x, y, width, height}.
struct Crop { float x, y, w, h; };
inline Crop wideCrop(float w, float h, float aspect = 16.f / 9.f) {
    if (w <= 0.f || h <= 0.f) return {0.f, 0.f, 0.f, 0.f};
    if (w / h > aspect) { // wider: the middle of its width
        const float cw = h * aspect;
        return {(w - cw) * 0.5f, 0.f, cw, h};
    }
    const float ch = w / aspect; // taller: the middle of its height
    return {0.f, (h - ch) * 0.5f, w, ch};
}

} // namespace cards::layout
