#pragma once
// The home screen's layouts (Y cycles them; Settings > Look > Home layout) and
// the geometry of the two new ones: Wheel (the row's selection and scroll,
// laid out differently) and Channels (a grid style). Pure (no UI): CardsActivity draws what these place.

#include "Settings.hpp"

#include <algorithm>
#include <cmath>

namespace cards::layout {

enum class Layout : int { Cards, Wheel, Grid, Channels, Cross, Count };

constexpr int kRowCards = 0, kRowWheel = 2; // (1 was Flow, then another look: both gone, read as Cards)
constexpr int kGridIcons = 0, kGridChannels = 1, kGridStyles = 2;

inline Layout current(const Settings& s) {
    if (s.xmbView) return Layout::Cross;
    if (s.gridView) return s.gridStyle == kGridChannels ? Layout::Channels : Layout::Grid;
    return s.rowStyle == kRowWheel ? Layout::Wheel : Layout::Cards;
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
        s.rowStyle = l == Layout::Wheel ? kRowWheel : kRowCards;
        return;
    }
}

// Y: Cards, Wheel, Grid, Channels, Cross, and round again.
inline Layout next(Layout l, int step = 1) {
    constexpr int n = static_cast<int>(Layout::Count);
    return static_cast<Layout>(((static_cast<int>(l) + step) % n + n) % n);
}

inline const char* name(Layout l) {
    switch (l) {
    case Layout::Wheel: return "Wheel";
    case Layout::Grid: return "Grid";
    case Layout::Channels: return "Channels";
    case Layout::Cross: return "Cross view";
    default: return "Cards";
    }
}

inline bool rowModel(Layout l) { return l == Layout::Cards || l == Layout::Wheel; }
inline bool gridModel(Layout l) { return l == Layout::Grid || l == Layout::Channels; }

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

// Settings > Wheel > Row size: small, medium, large.
inline float wheelSize(int size) { return size <= 0 ? 0.85f : size >= 2 ? 1.15f : 1.f; }

// left: the rows down the left side and the cover on the right (mirrored).
// The fall-off goes by distance on screen (rel x size), so bigger rows show fewer
// of them and smaller ones more, all on screen; the far rows never run off the side.
inline WheelSlot wheelSlot(float rel, float size = 1.f, bool left = false) {
    using namespace wheel;
    WheelSlot s;
    const float d = rel * size, a = std::abs(d);
    s.shown = a < kShown;
    const float k = std::min(a, kEnds) / kEnds;
    s.scale = (1.f - 0.3f * k) * size;
    s.alpha = (1.f - 0.75f * k) * std::clamp((kShown - a) / (kShown - kEnds), 0.f, 1.f);
    s.w = kW * s.scale;
    s.h = kH * s.scale;
    const float x = std::min(kX + kBend * d * d, 1260.f - s.w);
    s.x = left ? 1280.f - x - s.w : x;
    s.y = kMidY + d * kPitch - s.h * 0.5f;
    return s;
}

struct Box { float x, y, w, h; };
inline Box wheelCover(bool left = false) {
    using namespace wheel;
    return {left ? 1280.f - kCoverX - kCoverW : kCoverX, kCoverY, kCoverW, kCoverH};
}

// ---------------------------------------------------------------- Channels
// A grid style: wide tiles, 4 x 3 a page (Settings > Channels > Tiles: or 3 x 2, bigger).
struct GridCell { int cols, rows; float w, h, padX, padY; };
constexpr GridCell kChannelCell{4, 3, 268.f, 151.f, 22.f, 20.f};
constexpr GridCell kChannelCellBig{3, 2, 352.f, 198.f, 26.f, 28.f};
inline GridCell channelCell(int tiles) { return tiles == 1 ? kChannelCellBig : kChannelCell; }

// A channel opened (A on a tile): its picture large, a disc turning in the
// corner, Back and Start under it; L / R the channel before / after.
namespace channel {
constexpr Box kPanel{70.f, 46.f, 1140.f, 628.f};
constexpr Box kArt{100.f, 76.f, 1080.f, 456.f};       // the picture (cropped to its shape)
constexpr float kDiscX = 1040.f, kDiscY = 452.f, kDiscR = 104.f; // over the picture's bottom right
constexpr Box kBack{310.f, 570.f, 300.f, 76.f}, kStart{670.f, 570.f, 300.f, 76.f};
constexpr float kTurnsPerSecond = 0.6f;               // the disc
constexpr float kSwapSec = 0.22f;                     // L / R: the picture slides over
}
inline bool inBox(const Box& b, float x, float y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }
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
