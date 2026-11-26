# CardsNX + menu consolidation and menu feel — design

Source brief: crimson's "Menu Consolidation, Interface Refinement and Performance
Optimisation" plan (project thread, 2026-11-25). This document is the binding spec
for Phases 1 and 2 of that brief. Phase 3 (image, music, Discord and general
loading performance) gets its own spec and plan once a before-measurement exists
(see "Phase 3" below).

Guiding principle (from the brief, verbatim): **simplification without loss of
functionality: fewer unnecessary menu entries, better-organised options, smoother
interactions, and faster performance throughout the application.**

## 1. Research: which menu pattern fits

CardsNX is a controller-first 1280x720 console HOME menu (D-pad / sticks / A / B,
plus touch). Patterns evaluated against that:

| Pattern | Fit | Why |
|---|---|---|
| Dropdown / menubar | Poor | Needs a pointer and a persistent bar; no anchor on a card row. |
| Right-click context menu | Partial | The `+` sheet already *is* the context menu for the chosen card. Keep. |
| Sidebar / nav bar / tabs | Already used | Pages (ZL/ZR) and Settings tabs. Not for per-card actions. |
| Toolbar | Poor | Too few buttons on a pad; no hover. |
| **Action sheet with progressive disclosure** (iOS action sheets, Switch / PS5 "Options" menus, Steam Deck "…" menu) | **Best** | One modal per context, primary action first, related actions grouped behind one entry that opens a second level *in the same sheet*, B goes back one level. |

Decision: the `+` sheet stays the one per-card menu and becomes a two-level action
sheet. Groups open *inside the same sheet* (same card frame, art and title), never
as a different-looking popup, so every level looks and behaves the same.

Design rules that follow (apply to every level of the `+` sheet):

- Primary action (Play / Resume / Open / Get) is always first, full width, white.
- At most 9 entries on the top level after the primary one (2 columns x 5 rows), so
  its buttons never shrink below ~51 px (today the sheet can carry ~25 entries and
  buttons shrink to ~17 px). Move to… is the one level that can hold more (up to 10
  destinations with 8 custom pages, plus 3 entries); it is the only level allowed to
  go below that height.
- A group entry ends in "…" in its label and opens its level in place; the sheet's
  second line (under the card's name) shows the group's name; B (or the "B Back" hint) returns to the level
  above with the group's button still chosen; B on the top level closes.
- Destructive actions (Delete) are red and last on the top level.
- A group with exactly one entry is not shown as a group: that entry is shown in its
  place (no pointless extra press). Move to… is exempt (its destinations are the point).
- Empty groups are not shown.
- Icons: every entry keeps a glyph from the existing glyph set; groups use one glyph each.

## 2. The new `+` menu (top level)

In this order, each only when it applies to the chosen card:

1. Play / Resume / Open / Get (primary)
2. Update to {version} (a newer .nro exists)
3. Close software (running, HOME build only)
4. Favourite / Unfavourite
5. Rearrange (was "Move": reorder on this page)
6. **Move to…** (one destination selector, section 3)
7. **Appearance…** (section 4)
8. **Info…**: About this game, Note, Copy its music (Details is on the top level, after
   Close software: crimson's call after the first console test, 2026-11-26)
9. **More…**: Search games, Put on the panel button, Hide this card
10. Delete (red)

## 3. Move to… — the one place content moves

Replaces: "Move to Games", "Move to Homebrew", "Move to Media", "Move to custom
page", the "Move to custom page" picker, and "Remove from this page" / "Put back on
its page".

Level contents, in this order:

- One entry per destination page, in the user's page order (`Settings::pageOrder`),
  computed every time the level opens from one function (single source of truth):
  - every library page (Games, Homebrew, Media) and every custom page that is not a
    genre page, except the page the card is on now;
  - never the Nintendo page (system applets cannot be moved there);
  - hidden pages are included, marked "Hidden page"; moving there shows the page again;
  - a card that belongs to the Nintendo page (system applets) can only go to custom
    pages (as today: it cannot be placed on Games / Homebrew / Media).
- "Add a copy to a page…" (the existing Add-to-custom-page copy, unchanged).
- On a custom page: "Remove from this page" (copy) or "Put back on its page" (moved).
- "New page…" (opens Custom pages to create one).

Move semantics (custom-page bookkeeping, then placement):

1. Moving from a custom page takes the card off that page (its `titles` and `moved`).
2. The card leaves every other custom page it had been *moved* to (one place at a
   time). Copies (in `titles` only) elsewhere stay.
3. To a custom page: it is added to that page's `titles` and `moved` (off its own page).
4. To a library page: the existing Games / Homebrew / Media placement (`moveToPage`).
5. Item data, names, pictures, favourites, notes and order data are keyed by title id
   or path and are untouched by a move.
6. Feedback: success says "{game} moved to {page}" (custom) or the existing
   "{game} is on the {page} page now." (library); a destination that no longer
   exists when chosen says "Couldn't move {game}: that page isn't there any more."
   and changes nothing.

## 4. Appearance…

Rename, Card picture, Background picture, Title picture, Icon picture,
Show as a full card / Show as an icon (homebrew only), Download new pictures.
Each runs exactly the existing action.

## 5. Phase 2 — menu feel

- Stagger: every button of a sheet level reaches full opacity and position when the
  open animation ends, whatever the entry count (today entries 8+ never fully appear:
  the stagger delay 0.04 x i plus 0.7 exceeds 1).
- Level change: switching level keeps the card frame still (no scale bounce, no dim
  change) and only re-staggers the buttons, in at most 0.16 s.
- One set of timing constants for all popups (open 0.20 s, sheet 0.24 s, level 0.16 s),
  in one header; no popup open animation longer than 0.24 s.
- Closing: the `+` sheet and list popups fade out in 0.12 s without blocking input
  (the next screen takes input at once).
- Hand-off without flicker: when a `+` entry opens a list popup, the dimmed backdrop
  stays dimmed (no drop to clear and back).
- With Settings > motion off, everything is instant (as today).

## 6. Phase 3 (separate plan)

Image, music, Discord and general loading. Before any change, record a baseline with
the existing `perfLog` (sdmc:/config/CardsNX/perf.log: start-up marks and slow
frames) on the console, then plan from the measured hot spots, and measure again
after. This needs a run on crimson's console.

## Constraints

- No new dependencies. C++20, existing nxui renderer, existing glyph set.
- Every new user-facing English string gets a row in `projects/cards/romfs/lang.tsv`
  with all 9 languages; `python3 tools/check_cards_lang.py` must pass (it also fails
  on rows whose English is no longer in the code, so rows for removed strings go).
- Both targets that compile CardsActivity build: `SwitchU` (defines SWITCHU_MENU)
  and `CardsNX`.
- Pure logic goes in small headers under `projects/cards/src/` with host tests in
  `tests/`, registered in `tests/run_cards_personalization.sh` (existing pattern).
