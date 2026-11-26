#include "MenuMotion.hpp"
#include <cassert>

int main() {
    using namespace cards::motion;
    static_assert(kPopupOpen == 0.20f && kSheetOpen == 0.24f && kLevelChange == 0.16f && kClose == 0.12f);
    for (int n = 1; n <= 30; ++n) {
        for (int i = 0; i < n; ++i) {
            assert(stagger(1.f, i, n) == 1.f);
            assert(stagger(0.f, i, n) == 0.f);
            assert(stagger(-1.f, i, n) == 0.f);
            assert(stagger(2.f, i, n) == 1.f);
            float prev = 0.f;
            for (int k = 0; k <= 100; ++k) {
                const float t = 0.01f * static_cast<float>(k);
                const float v = stagger(t, i, n);
                assert(v >= prev && v >= 0.f && v <= 1.f);
                prev = v;
                assert(stagger(t, 0, n) >= stagger(t, n - 1, n));
            }
        }
    }
    return 0;
}
