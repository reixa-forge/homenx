# Home layouts

Design: [docs/superpowers/specs/2026-11-26-home-layouts-design.md](superpowers/specs/2026-11-26-home-layouts-design.md).

Y on HOME goes to the next layout, round in this order. Settings > Look > Home layout picks one directly.

| Layout | What it looks like | Moving around |
|---|---|---|
| Cards | The card row (as before) | Left / Right |
| **Flow** (new) | 3D covers: the chosen one faces you, the others turned away on both sides, with reflections | Left / Right, L / R, swipe |
| **Wheel** (new) | The chosen game's cover large on the left; the page's games as glass rows on an arc on the right | Up / Down (Left / Right too), up on the first game reaches the top bar, swipe up / down |
| Grid | Wii U style icons (as before) | D-pad over the tiles |
| **Channels** (new) | A grid of 12 wide tiles a page; the chosen one shows the game's background picture and its name | As the Grid |
| Cross view | PSP / PS3 style (as before) | As before |

Everything else is the same in every layout: A plays, + opens the options, Rearrange,
the launch animations, the pages (ZL / ZR) and touch.

- Flow and Wheel use the card row's selection and scroll, so switching between Cards,
  Flow and Wheel keeps your place. Between the row and the grid it morphs as before.
- Wheel has no bottom panel (its name and details are under the cover).
- Channels uses the Grid's pictures (the console's icons) and the background picture
  that is loaded anyway; it loads nothing extra. The Grid's size setting doesn't apply to it.
- Settings: `rowStyle` (0 cards, 1 flow, 2 wheel) and `gridStyle` (0 icons, 1 channels) in
  CardsNX's settings file. Older settings files open as before.

## Check on the console

1. Y cycles Cards, Flow, Wheel, Grid, Channels, Cross view, Cards; the message names each.
2. Flow: covers turn as you scroll and touch-swipe; the front one has the ring; apps show their icon.
3. Wheel: Up / Down scroll and repeat when held; up on the first game reaches the top bar; A plays.
4. Channels: 12 tiles a page, paging with L / R and at the edges; Rearrange works.
5. Each layout launches a game with your launch animation.
