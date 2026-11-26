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
        const Layout order[] = {Layout::Wheel, Layout::Grid, Layout::Channels, Layout::Cross, Layout::Cards};
        for (Layout want : order) assert((l = next(l)) == want);
        assert(next(Layout::Cards, -1) == Layout::Cross);
    }
    {
        Settings s; // old settings files: no rowStyle / gridStyle -> what they were
        assert(current(s) == Layout::Cards);
        s.rowStyle = 1; // (looks since taken out) -> Cards
        assert(current(s) == Layout::Cards);
        s.rowStyle = 0;
        s.gridView = true;
        assert(current(s) == Layout::Grid);
        s.xmbView = true;
        assert(current(s) == Layout::Cross);
        // The grid's size and the cross style are kept through a change.
        Settings t;
        t.gridLayout = 2;
        t.crossStyle = 1;
        apply(t, Layout::Channels);
        apply(t, Layout::Wheel);
        assert(t.gridLayout == 2 && t.crossStyle == 1 && t.gridStyle == kGridChannels);
    }
    assert(rowModel(Layout::Cards) && rowModel(Layout::Wheel) && !rowModel(Layout::Grid));
    assert(gridModel(Layout::Channels) && !gridModel(Layout::Cross));

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
    // Wheel, mirrored and sized: the left one is the right one's mirror image;
    // sizes scale the rows and their spacing; the cover swaps sides with them.
    for (int size = 0; size <= 2; ++size) {
        const float k = wheelSize(size);
        for (float rel = -4.f; rel <= 4.f; rel += 0.5f) {
            const WheelSlot r = wheelSlot(rel, k), l = wheelSlot(rel, k, true);
            assert(near(l.x, 1280.f - r.x - r.w) && near(l.y, r.y) && near(l.w, r.w));
            if (r.shown) assert(r.x >= 0.f && r.x + r.w <= 1280.f && r.y >= 0.f && r.y + r.h <= 720.f);
        }
        const WheelSlot a = wheelSlot(0.f, k), b = wheelSlot(1.f, k);
        assert(near((b.y + b.h * 0.5f) - (a.y + a.h * 0.5f), wheel::kPitch * k));
        const Box cr = wheelCover(false), cl = wheelCover(true);
        assert(cr.x + cr.w + 20.f < wheelSlot(0.f, k).x);                  // the cover clear of the rows
        assert(cl.x > wheelSlot(0.f, k, true).x + wheelSlot(0.f, k, true).w + 20.f);
    }
    assert(wheelSize(0) < wheelSize(1) && wheelSize(1) < wheelSize(2) && near(wheelSize(1), 1.f));
    // The channel screen: its parts inside the panel, the buttons side by side, the disc on the picture.
    {
        using namespace channel;
        auto inside = [](const Box& in, const Box& out) {
            return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h;
        };
        assert(inside(kArt, kPanel) && inside(kBack, kPanel) && inside(kStart, kPanel));
        assert(kBack.x + kBack.w < kStart.x && near(kBack.y, kStart.y));
        assert(kArt.y + kArt.h < kBack.y);
        assert(kDiscX - kDiscR >= kArt.x && kDiscX + kDiscR <= kArt.x + kArt.w && kDiscY + kDiscR <= kPanel.y + kPanel.h);
        assert(near(kBack.x + kBack.w * 0.5f + kStart.x + kStart.w * 0.5f, 2.f * 640.f)); // centred
        assert(inBox(kStart, kStart.x + 1.f, kStart.y + 1.f) && !inBox(kStart, kBack.x + 1.f, kBack.y + 1.f));
    }
    // Channels' big tiles fit too.
    {
        const GridCell c = channelCell(1);
        assert(c.cols * c.w + (c.cols - 1) * c.padX < 1200.f && c.rows * c.h + (c.rows - 1) * c.padY < 482.f);
        assert(near(c.w, c.h));
        assert(channelCell(0).cols == 4 && channelCell(5).cols == 4);
    }
    // Channels: four square tiles fit across with their gaps; the 16:9 crop is centred.
    {
        const GridCell c = kChannelCell;
        assert(c.cols * c.w + (c.cols - 1) * c.padX < 1280.f - 2.f * 40.f);
        assert(c.rows * c.h + (c.rows - 1) * c.padY <= 482.f); // (the grid's room)
        assert(near(c.w, c.h));
        const Crop sqIn = wideCrop(1280.f, 720.f, 1.f); // a wide picture in a square tile: its middle
        assert(near(sqIn.w, 720.f) && near(sqIn.h, 720.f) && near(sqIn.x, 280.f));
        const Crop sq = wideCrop(256.f, 256.f);
        assert(near(sq.x, 0.f) && near(sq.w, 256.f) && near(sq.h, 144.f) && near(sq.y, 56.f));
        const Crop wide = wideCrop(1920.f, 600.f);
        assert(near(wide.h, 600.f) && near(wide.w, 1066.67f, 0.1f) && near(wide.x, (1920.f - wide.w) * 0.5f));
        const Crop none = wideCrop(0.f, 10.f);
        assert(none.w == 0.f && none.h == 0.f);
    }
    return 0;
}
