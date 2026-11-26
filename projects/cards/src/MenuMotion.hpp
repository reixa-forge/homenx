#pragma once
// One set of timings for every popup, and the stagger of a list's entries.
#include <algorithm>

namespace cards::motion {

// Seconds.
constexpr float kPopupOpen = 0.20f;   // a small popup (menu, tasks, log, quick list)
constexpr float kSheetOpen = 0.24f;   // a sheet (the longest any popup takes to open)
constexpr float kLevelChange = 0.16f; // a level change inside an open sheet
constexpr float kClose = 0.12f;       // close fade

// Progress 0..1 of entry i of n at overall progress t. Entries start up to 0.3
// apart and each takes 0.7, so every entry reaches 1 exactly when t reaches 1.
inline float stagger(float t, int i, int n) {
    if (t >= 1.f) return 1.f;
    const float delay = n > 1 ? 0.3f * static_cast<float>(i) / static_cast<float>(n - 1) : 0.f;
    return std::clamp((t - delay) / 0.7f, 0.f, 1.f);
}

} // namespace cards::motion
