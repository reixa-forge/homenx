# CardsNX home layouts: Flow, Wheel, Channels — design

Source: crimson, project thread 2026-11-26 ("research the most used and most loved
menu types and add these menus to the options to switch between… things like 3D
flow"), then the decision card: **Flow, Wheel, Channels**. After the first console
test crimson dropped Flow ("flow isnt nice") for a Deck (Steam Deck style) layout, then
dropped Deck too ("looks like cards just lower") and asked for a Wii-style channel screen
and Settings tabs for the layouts: section 3 now describes those.

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

- `enum class Layout { Cards, Wheel, Grid, Channels, Cross }`.
- Stored as today's flags plus two new ints, so old settings files keep working:
  `Settings::rowStyle` (0 cards, 2 wheel; 1, the removed looks, reads as cards; used when neither grid nor cross)
  and `Settings::gridStyle` (0 icons, 1 channels; used when the grid is on).
- `current(const Settings&)`, `apply(Settings&, Layout)`, `next(Layout)` (the Y cycle:
  Cards → Wheel → Grid → Channels → Cross → Cards), `name(Layout)`.
- Geometry, pure and tested: `wheelSlot(rel, size, left)`, `wheelCover(left)`, `channelCell(tiles)`,
  the channel screen's boxes (`layout::channel`).

Settings > Look gets one **Home layout** choice row (all five, left / right), and Y keeps
cycling. The existing Grid view / Cross view toggles stay and agree with it.

## 3. The channel screen and the layouts' Settings tabs

- Channels, A on a tile (or tapping the chosen one): a panel zooms out of the middle with
  the game's picture large (background picture, else cover, else icon), its name on a
  shade along the bottom, a silver disc turning over the picture's corner for a game
  (the game's icon fills it and turns: nxui `drawTextureDisc` turns the texture coordinates
  inside the circle mask; the light on the disc stays put), and big Back and Play buttons.
  Left / Right choose, A presses, B back; L / R (and side arrows / taps) the channel before /
  after with a slide. HOME behind stays still (no page slide or sheen; nearly hidden) and
  catches up to the chosen tile's page when the screen closes.
  + the options over it; it closes for Move, a launch, or a layout change.
  While it's open (and the disc on) it asks for full frame rate (`wantsFullRate`): the idle
  tiers had thinned the frames a few seconds after the last press and the disc stuttered.
- Settings > Wheel: on / off, Side (rows right or left, mirrored), Row size (0.85 / 1 / 1.15:
  scale and pitch; the fall-off goes by on-screen distance so all rows stay on screen).
- Backgrounds are kept at the screen's real width (1280 handheld, 1920 docked; they were
  capped at 1280 and stretched 1.5x on a TV). The channel screen falls back to the 96 px
  thumbnail last.
- Settings > Channels: on / off, Tiles (4 x 3 or 3 x 2), Tile picture (Background, the default:
  a new picture kind, `kTile`: the background picture at card size (~450 px docked), asked for
  the whole page and the pages either side and kept like the grid's icons, so a tile never swaps
  pictures as the cursor moves (it had used the card art or the background, whichever was in,
  and both come and go); the icon until it comes or when there's no wide one. Icon: the console's icon), Channel screen, Spinning disc,
  Every channel's name.
- The scrolling light on the chosen card is masked to its rounded corners
  (nxui `beginRoundedMask` / `endRoundedMask`, the existing SDF shape mask).

(Deck and Flow, both dropped after console tests, are removed with their code.)

## 4. Wheel

- One side (Settings > Wheel > Side): the page's games on a gentle arc, one per 76 px, the chosen one in the
  middle at full size on glass with an accent ring; others smaller (down to 0.7) and
  fainter (down to 0.25) toward the ends; up to 4 above and 4 below. Each shows its
  title picture (logo) when there is one, else its name.
- The other side: the chosen game's cover, large, with its name under it (no shadow). No bottom panel (A
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
