# CardsNX home layouts: Flow, Wheel, Channels — design

Source: crimson, project thread 2026-11-26 ("research the most used and most loved
menu types and add these menus to the options to switch between… things like 3D
flow"), then the decision card: **Flow, Wheel, Channels**.

## 1. Research summary

CardsNX already has four home layouts, cycled with Y: **Cards** (the row), **Grid**
(Wii U icons), **Cross** (PSP / PS3 XMB) and the cross view's **Row** style (PS4).
The best-loved patterns it lacks, and why these three were chosen:

| Pattern | Evidence | Picked |
|---|---|---|
| Cover flow (3D, side covers turned away, floor reflection) | iTunes / Finder origin; standard view in Kodi, Pegasus and LaunchBox themes | **Flow** |
| Vertical wheel of logos beside big art | The arcade front-end look (HyperSpin, ES-DE carousels) | **Wheel** |
| Channel grid (big rounded 16:9 tiles, 12 a page) | Wii; still named a favourite in UI retrospectives (vale.rocks, 2026) | **Channels** |
| Blades | Won Pure Xbox's 2020 "best Xbox dashboard" poll (33% of 810) | later |
| Shelf (spines) | Art-book style themes | later |
| Tiles (Metro) | Divisive at the time | later |

All three are original renditions of a pattern, drawn with CardsNX's own art,
glass, accent and fonts; no third-party art or names.

## 2. One layout model (single source of truth)

`projects/cards/src/HomeLayout.hpp` (pure, host-tested):

- `enum class Layout { Cards, Flow, Wheel, Grid, Channels, Cross }`.
- Stored as today's flags plus two new ints, so old settings files keep working:
  `Settings::rowStyle` (0 cards, 1 flow, 2 wheel; used when neither grid nor cross)
  and `Settings::gridStyle` (0 icons, 1 channels; used when the grid is on).
- `current(const Settings&)`, `apply(Settings&, Layout)`, `next(Layout)` (the Y cycle:
  Cards → Flow → Wheel → Grid → Channels → Cross → Cards), `name(Layout)`.
- Geometry, pure and tested: `flowSlot(rel)`, `wheelSlot(rel)`, `channelCell()`.

Settings > Look gets one **Home layout** choice row (all six, left / right), and Y keeps
cycling. The existing Grid view / Cross view toggles stay and agree with it.

## 3. Flow

- The chosen cover faces you in the middle (x 640), 1.25x a card's size; the others
  stand turned away on both sides, overlapping, each next one a step further out
  and slightly darker; at most 5 a side drawn.
- Turned covers are trapezoids (near edge full height, far edge 80%), drawn with a
  new `nxui::Renderer::drawTextureQuad` (four corners, the quad cut into vertical
  strips so the picture doesn't bend at the diagonal). A reflection under each cover:
  the picture flipped, fading from 30% to 0 over a third of its height.
- Same input as Cards: left / right / L / R / touch drag / fling / tap, the same
  selection and `m_scroll`; turning glides with the scroll (no extra state).
- The bottom panel, the top bar, Rearrange, launch animations and the + sheet work as
  in Cards (they use `cardRect`, which returns each cover's bounding box).

## 4. Wheel

- Right half: the page's games on a gentle arc, one per 84 px, the chosen one in the
  middle at full size on glass with an accent ring; others smaller (down to 0.7) and
  fainter (down to 0.25) toward the ends; up to 4 above and 4 below. Each shows its
  title picture (logo) when there is one, else its name.
- Left: the chosen game's cover, large, with its name under it. No bottom panel (A
  plays, + opens the options as everywhere).
- Up / down (and left / right) move along the wheel, repeating while held; up on the
  first game reaches the top bar; L / R jump a screen (8). Touch: drag up / down
  scrolls, tap a row to choose it, tap the chosen one to play.

## 5. Channels

- A grid style: 4 x 3 tiles a page, 16:9 (268 x 151), rounded 22 px, white frame,
  same paging, moving, touch and input as the Grid (the grid's cell becomes a width
  and a height).
- A tile shows the game's icon art filling it (cropped to 16:9 from the middle);
  the chosen tile shows the game's background picture instead once it's loaded (it is
  loaded anyway for the screen behind), its name in a strip along its bottom, and
  grows 1.06 with the accent ring. No extra pictures are loaded for Channels.

## 6. Switching

- Y and the Settings row change layout through `apply`. Grid ↔ row keeps today's
  morph; any other change fades the new layout in (the page fade, 0.2 s). The
  selection, page and scroll position are kept. With motion off, at once.
- The new strings get lang.tsv rows in all 9 languages.

## 7. Not in scope

Blades, Shelf and Tiles (later, on request). The cross view is unchanged. Loading
performance (Phase 3) is separate.

## Constraints

As the + menu spec: no new dependencies, C++20, nxui; both targets (SwitchU, CardsNX)
build; pure logic in headers with host tests registered in
`tests/run_cards_personalization.sh`; `python3 tools/check_cards_lang.py` passes.
