#pragma once
// The home screen's layouts (Y cycles them; Settings > Look > Home layout) and
// the geometry of the three that share the card row's model: Flow and Wheel
// (the row's selection and scroll, laid out differently) and Channels (a grid
// style). Pure (no UI): CardsActivity draws what these place.

#include "Settings.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace cards::layout {

enum class Layout : int { Cards, Flow, Wheel, Grid, Channels, Cross, Count };

constexpr int kRowCards = 0, kRowFlow = 1, kRowWheel = 2, kRowStyles = 3;
constexpr int kGridIcons = 0, kGridChannels = 1, kGridStyles = 2;

inline Layout current(const Settings& s) {
    if (s.xmbView) return Layout::Cross;
    if (s.gridView) return s.gridStyle == kGridChannels ? Layout::Channels : Layout::Grid;
    return s.rowStyle == kRowFlow ? Layout::Flow : s.rowStyle == kRowWheel ? Layout::Wheel : Layout::Cards;
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
        s.rowStyle = l == Layout::Flow ? kRowFlow : l == Layout::Wheel ? kRowWheel : kRowCards;
        return;
    }
}

// Y: Cards, Flow, Wheel, Grid, Channels, Cross, and round again.
inline Layout next(Layout l, int step = 1) {
    constexpr int n = static_cast<int>(Layout::Count);
    return static_cast<Layout>(((static_cast<int>(l) + step) % n + n) % n);
}

inline const char* name(Layout l) {
    switch (l) {
    case Layout::Flow: return "Flow";
    case Layout::Wheel: return "Wheel";
    case Layout::Grid: return "Grid";
    case Layout::Channels: return "Channels";
    case Layout::Cross: return "Cross view";
    default: return "Cards";
    }
}

inline bool rowModel(Layout l) { return l == Layout::Cards || l == Layout::Flow || l == Layout::Wheel; }
inline bool gridModel(Layout l) { return l == Layout::Grid || l == Layout::Channels; }

struct Point { float x, y; };

// ---------------------------------------------------------------- Flow
// The chosen cover faces you in the middle; the others stand turned away on
// both sides. rel: the cover's index minus the row's scroll (fractional while
// it glides). A slot's corners go top-left, top-right, bottom-right,
// bottom-left; a turned cover's outer edge is shorter (it's further away).
namespace flow {
constexpr float kCentreX = 640.f, kMidY = 305.f;
constexpr float kW = 276.f, kH = 391.f;       // the front cover (1.15 of a card)
constexpr float kSideH = 0.86f;               // a turned cover's near edge, of kH
constexpr float kFar = 0.80f;                 // its far edge, of its near edge
constexpr float kSideW = 0.46f;               // its width on screen, of kW
constexpr float kGap = 205.f, kPitch = 88.f;  // the first side cover's middle from the centre; then each next one
constexpr float kShown = 5.5f;                // covers further than this aren't drawn
constexpr float kSwipePitch = 140.f;          // touch: pixels dragged per cover
}

struct FlowSlot {
    std::array<Point, 4> corners; // TL, TR, BR, BL
    float turn = 0.f;             // -1 turned (on the left) .. 0 facing you .. 1 turned (on the right)
    float shade = 0.f;            // 0..0.6 darker the further it is
    bool shown = false;
    // Its box on screen (touch, the rings, the launch).
    float left() const { return std::min(corners[0].x, corners[3].x); }
    float right() const { return std::max(corners[1].x, corners[2].x); }
    float top() const { return std::min(corners[0].y, corners[1].y); }
    float bottom() const { return std::max(corners[2].y, corners[3].y); }
};

inline FlowSlot flowSlot(float rel) {
    using namespace flow;
    FlowSlot s;
    const float a = std::abs(rel);
    s.shown = a <= kShown;
    const float side = rel < 0.f ? -1.f : 1.f;
    const float t = std::min(a, 1.f);                         // facing -> turned, over the first step
    const float smooth = t * t * (3.f - 2.f * t);
    s.turn = side * smooth;
    const float cx = kCentreX + side * (kGap * smooth + kPitch * std::max(0.f, a - 1.f));
    const float w = kW + (kW * kSideW - kW) * smooth;
    const float hNear = kH + (kH * kSideH - kH) * smooth;
    const float hFar = hNear * (1.f + (kFar - 1.f) * smooth);
    const float x0 = cx - w * 0.5f, x1 = cx + w * 0.5f;
    // Turned away on the left: the right edge is the near one; on the right, the left.
    const float hL = side < 0.f ? hFar : hNear, hR = side < 0.f ? hNear : hFar;
    s.corners = {Point{x0, kMidY - hL * 0.5f}, Point{x1, kMidY - hR * 0.5f}, Point{x1, kMidY + hR * 0.5f},
                 Point{x0, kMidY + hL * 0.5f}};
    s.shade = std::min(0.6f, 0.18f * smooth + 0.08f * std::max(0.f, a - 1.f));
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
