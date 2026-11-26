# Home layouts

Design: [docs/superpowers/specs/2026-11-26-home-layouts-design.md](superpowers/specs/2026-11-26-home-layouts-design.md).

Y on HOME goes to the next layout, round in this order. Settings > Look > Home layout picks one directly.

| Layout | What it looks like | Moving around |
|---|---|---|
| Cards | The card row (as before) | Left / Right |
| **Wheel** (new) | The chosen game's cover large on one side; the page's games as glass rows on an arc on the other | Up / Down (Left / Right too), up on the first game reaches the top bar, swipe up / down |
| Grid | Wii U style icons (as before) | D-pad over the tiles |
| **Channels** (new) | A grid of square rounded tiles (12 or 6 a page); the chosen one shows the game's background picture and its name. A opens the channel screen | As the Grid |
| Cross view | PSP / PS3 style (as before) | As before |

Everything else is the same in every layout: A plays (Channels: opens the channel), + opens the
options, Rearrange, the launch animations, the pages (ZL / ZR) and touch.

- Wheel uses the card row's selection and scroll, so switching between Cards and Wheel
  keeps your place. Between the row and the grid it morphs as before.
- Wheel has no bottom panel (its name and details are drawn beside the cover).
- Channels uses the Grid's pictures (the console's icons) and the background picture
  that is loaded anyway; it loads nothing extra. The Grid's size setting doesn't apply to it.

## The channel screen (Channels, A)

The channel's picture large (its background picture, else its cover or icon), its name,
a disc turning for a game, and big **Back** and **Play** buttons (Play chosen first).

- Left / Right choose the button, A presses it, B goes back to the tiles.
- L / R (ZL / ZR, or the arrows at the sides) go to the channel before / after; the tiles behind stay still and are on that channel's page when you go back.
- + opens the game's options over it; X closes the open game.
- Touch: the buttons and the arrows; outside the panel goes back.

## Settings

- **Wheel** tab: Wheel on / off, Side (rows on the right or the left; the cover takes the
  other), Row size (Small, Medium, Large; smaller shows more games).
- **Channels** tab: Channels on / off, Tiles (4 x 3, or 3 x 2 bigger), Channel screen (off:
  A starts the game at once), Spinning disc, Every channel's name (off: only the chosen one's).
- The Grid view tab's toggle is on only for the Grid itself (not Channels).
- In the settings file: `rowStyle` (0 cards, 2 wheel; 1 is an old look, read as cards),
  `gridStyle` (0 icons, 1 channels), `wheelLeft`, `wheelSize`, `channelScreen`, `channelDisc`,
  `channelNames`, `channelTiles`. Older settings files open as before.

## Check on the console

1. Y cycles Cards, Wheel, Grid, Channels, Cross view, Cards; the message names each.
2. Wheel: Up / Down scroll and repeat when held; up on the first game reaches the top bar; A plays.
   Settings > Wheel > Side and Row size change it at once.
3. Channels: 12 tiles a page (6 with 3 x 2), paging with L / R and at the edges; Rearrange works.
4. Channels, A: the channel screen; L / R change channel; Play starts it; B is back on the tiles.
5. Scrolling: the light that crosses the chosen card keeps to its rounded corners.
6. Each layout launches a game with your launch animation.
