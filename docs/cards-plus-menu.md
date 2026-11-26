# The + menu: two levels

Design: [docs/superpowers/specs/2026-11-26-plus-menu-consolidation-design.md](superpowers/specs/2026-11-26-plus-menu-consolidation-design.md).
The + sheet used to be one long list (up to 17 buttons, the last ones never fully
shown). It now has a short top level and four groups that open in the same sheet.
B goes back one level; + and X close from any level; touching outside the card closes.

## Where each old entry went

| Old entry | Now |
|---|---|
| Play / Resume / Open / Get, Update to {version} | Top level, first |
| Close software (SwitchU) | Top level |
| Favourite / Unfavourite | Top level |
| Move | Top level, renamed **Rearrange** |
| Delete | Top level, last |
| Move to custom page, Add to custom page, Move to {page} | **Move to…** |
| Remove from this page / Put back on its page | **Move to…** |
| Rename, Card picture, Background picture, Title picture, Icon picture, Show as a full card / icon, Download new pictures | **Appearance…** |
| Details, About this game, Note, Copy its music | **Info…** |
| Search games, Put on the panel button (SwitchU), Hide this card | **More…** |

A group with only one entry is not shown as a group; its entry sits on the top level.
**Move to…** appears only for installed games.

## How Move to… works

- It lists every page the game can go to: Games, Homebrew, Media and your custom pages,
  in page order. The page it is on, the Nintendo page and genre pages are left out.
- Moving to a library page from a custom page puts the game back there and takes it off
  the custom page it was moved to.
- A game that is only a copy on custom page A, moved to page B, stays a copy on A.
- **Add a copy to a page…** keeps the game where it is and adds it to a custom page.
- **New page…** opens Settings > Home > Custom pages.
- A hidden page is shown with "Hidden page" as the row's value; moving there unhides it.
- If the target page is gone when you press A (for example a page was deleted), nothing
  moves and you see "Couldn't move {game}: that page isn't there any more."
- A cartridge game that is inserted but not installed only gets custom pages as destinations.

## Timings

Open: lists 0.20 s, sheets 0.24 s. Level change inside the sheet 0.16 s (only the buttons
re-stagger). Close fade 0.12 s, and input is not held while it fades. A list opened from +
keeps the backdrop dimmed, so there is no flash. HOME and the lock screen close menus
instantly. Every + button now finishes appearing, however many there are. With motion off,
everything is instant.

## Results

- `bash tests/run_cards_personalization.sh` passes, including "Move destination tests passed.",
  "Plus menu model tests passed." and "Menu motion tests passed." Last lines:
  "Grid navigation tests passed." / "Widget model tests passed."
- `python3 tools/check_cards_lang.py`: `ok: 943 rows, 9 languages`.
- SwitchU and CardsNX were built with devkitA64 at 6e8e62a (baseline), ee6eb2a, 093c57e,
  50fb1eb and 6f8254d: "build ok" every time, no new warnings in projects/cards/src.
- Translation change: German "About" is now "Über" (it clashed with the new "Info" row).

## Check on the console

1. A game moved to a custom page, then sent to Games: it shows on Games and is gone from the custom page.
2. A game that is a copy on page A, moved from Games to page B: still on A, off Games, on B.
3. Delete a page while Move to… is open, then press A on it: no move, failure message.
4. B inside a group returns to the top level with that group selected; + and X close from every level; touching outside closes.
5. A sheet with 10 or more buttons: all buttons fully visible once it has opened.
6. A Nintendo-page applet moved to a custom page.
7. Move a game to a hidden page: the page reappears.
8. Motion off: menus open and close instantly, nothing stays dim or stuck.

## Remaining

- Phase 3 (image, music, Discord and general loading speed) is not started. It needs a
  baseline: turn on the perf log in Settings, use the console for a while, and send
  `sdmc:/config/CardsNX/perf.log`.
- Not tested on hardware yet.
- When a list opens from +, the backdrop steps from 0.5 to 0.42 dim; it may be smoothed in
  the final review fixes.
