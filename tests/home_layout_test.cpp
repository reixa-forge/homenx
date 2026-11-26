#include "HomeLayout.hpp"

#include <cassert>
#include <cmath>

using namespace cards;
using namespace cards::layout;

static bool near(float a, float b, float eps = 0.01f) { return std::abs(a - b) <= eps; }

int main()
{
    // The model: every layout applies and reads back; the cycle visits all six.
    for (int i = 0; i < static_cast<int>(Layout::Count); ++i) {
        const Layout l = static_cast<Layout>(i);
        for (int from = 0; from < static_cast<int>(Layout::Count); ++from) {
            Settings s;
            apply(s, static_cast<Layout>(from));
            apply(s, l);
            assert(current(s) == l);
            assert(!(s.xmbView && s.gridView));
        }
    }
    {
        Layout l = Layout::Cards;
        const Layout order[] = {Layout::Deck, Layout::Wheel, Layout::Grid, Layout::Channels, Layout::Cross, Layout::Cards};
        for (Layout want : order) assert((l = next(l)) == want);
        assert(next(Layout::Cards, -1) == Layout::Cross);
    }
    {
        Settings s; // old settings files: no rowStyle / gridStyle -> what they were
        assert(current(s) == Layout::Cards);
        s.gridView = true;
        assert(current(s) == Layout::Grid);
        s.xmbView = true;
        assert(current(s) == Layout::Cross);
        // The grid's size and the cross style are kept through a change.
        Settings t;
        t.gridLayout = 2;
        t.crossStyle = 1;
        apply(t, Layout::Channels);
        apply(t, Layout::Deck);
        assert(t.gridLayout == 2 && t.crossStyle == 1 && t.gridStyle == kGridChannels);
    }
    assert(rowModel(Layout::Deck) && rowModel(Layout::Wheel) && !rowModel(Layout::Grid));
    assert(gridModel(Layout::Channels) && !gridModel(Layout::Cross));

    // Deck: the chosen capsule first, at the left edge, larger; the next ones
    // to its right 20 px apart; the ones before it off to the left; all
    // standing on the same bottom line; continuous as the row glides.
    {
        const DeckSlot c = deckSlot(0.f);
        assert(c.shown && near(c.chosen, 1.f) && near(c.x, deck::kLeft));
        assert(near(c.w, deck::kW * deck::kGrow) && near(c.h, deck::kH * deck::kGrow));
        assert(near(c.y + c.h, deck::kBottom));
        assert(c.y > deck::kInfoY + 10.f); // (the name and details above it)
        DeckSlot prev = c;
        for (int i = 1; i <= 6; ++i) {
            const DeckSlot d = deckSlot(static_cast<float>(i));
            assert(near(d.chosen, 0.f) && near(d.w, deck::kW) && near(d.y + d.h, deck::kBottom));
            assert(near(d.x - (prev.x + prev.w), 20.f));
            prev = d;
        }
        const DeckSlot before = deckSlot(-1.f);
        assert(near(c.x - (before.x + before.w), 20.f));
        assert(!deckSlot(-2.f).shown && !deckSlot(7.f).shown);
        for (float x = -1.5f; x <= 6.f; x += 0.01f) {
            const DeckSlot a = deckSlot(x), b = deckSlot(x + 0.01f);
            assert(std::abs(a.x - b.x) < 4.f && std::abs(a.w - b.w) < 1.f);
            if (a.shown) assert(a.y >= 0.f && a.y + a.h <= 720.f);
        }
    }
    // Wheel: the middle row is full size and opaque; rows get smaller and
    // fainter both ways, bend right, and step down by the pitch.
    {
        const WheelSlot m = wheelSlot(0.f);
        assert(m.shown && near(m.scale, 1.f) && near(m.alpha, 1.f) && near(m.x, wheel::kX));
        assert(near(m.y + m.h * 0.5f, wheel::kMidY));
        for (int i = 1; i <= 4; ++i) {
            const WheelSlot d = wheelSlot(static_cast<float>(i)), u = wheelSlot(static_cast<float>(-i)),
                            p = wheelSlot(static_cast<float>(i - 1));
            assert(d.shown && u.shown);
            assert(d.scale < p.scale && near(d.scale, u.scale) && near(d.alpha, u.alpha));
            assert(d.x > p.x && near(d.x, u.x));
            assert(near((d.y + d.h * 0.5f) - (p.y + p.h * 0.5f), wheel::kPitch));
            assert(d.x + d.w <= 1280.f && u.y >= 0.f && d.y + d.h <= 720.f);
        }
        assert(near(wheelSlot(4.f).scale, 0.7f) && near(wheelSlot(4.f).alpha, 0.25f));
        assert(!wheelSlot(4.5f).shown && !wheelSlot(-5.f).shown);
        assert(wheelSlot(4.4f).alpha < wheelSlot(4.f).alpha);
        // The cover on the left stays clear of the rows, and on screen with its two lines under it.
        assert(wheel::kCoverX + wheel::kCoverW + 40.f < wheel::kX);
        assert(wheel::kCoverY > 90.f && wheel::kCoverY + wheel::kCoverH + 80.f < 720.f);
    }
    // Channels: four wide tiles fit across with their gaps; the crop is 16:9 and centred.
    {
        const GridCell c = kChannelCell;
        assert(c.cols * c.w + (c.cols - 1) * c.padX < 1280.f - 2.f * 40.f);
        assert(c.rows * c.h + (c.rows - 1) * c.padY < 560.f);
        assert(near(c.w / c.h, 16.f / 9.f, 0.02f));
        const Crop sq = wideCrop(256.f, 256.f);
        assert(near(sq.x, 0.f) && near(sq.w, 256.f) && near(sq.h, 144.f) && near(sq.y, 56.f));
        const Crop wide = wideCrop(1920.f, 600.f);
        assert(near(wide.h, 600.f) && near(wide.w, 1066.67f, 0.1f) && near(wide.x, (1920.f - wide.w) * 0.5f));
        const Crop none = wideCrop(0.f, 10.f);
        assert(none.w == 0.f && none.h == 0.f);
    }
    return 0;
}
