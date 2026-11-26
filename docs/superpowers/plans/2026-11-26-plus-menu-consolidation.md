# CardsNX + Menu Consolidation and Menu Feel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn CardsNX's ~25-entry `+` sheet into a compact two-level action sheet with one central Move to… selector and one Appearance… group, then make every popup open, change level and close smoothly and consistently.

**Architecture:** Decisions that can be tested off-console live in three small pure headers in `projects/cards/src/` (`PlusMenu.hpp` = which entries a card's sheet shows, `MoveTargets.hpp` = the one list of destination pages and the custom-page bookkeeping of a move, `MenuMotion.hpp` = timing constants and the stagger curve), each with a host test in `tests/`. `CardsActivity` maps those entries to the existing actions and draws the levels inside the existing `+` sheet (`m_optItems`, `drawOptions`, `updateOptions`, `optionsTouch`).

**Tech Stack:** C++20, devkitPro/libnx, nxui (in-repo renderer), xmake; host tests with the system `c++`, ASan/UBSan.

**Spec:** `docs/superpowers/specs/2026-11-26-plus-menu-consolidation-design.md`

## Global Constraints

- Simplification without loss of functionality: every action reachable from today's `+` sheet stays reachable from the new one, running the same code.
- Top level of the `+` sheet, in this order, each only when it applies: Play/Resume/Open/Get, Update to {version}, Close software, Favourite/Unfavourite, Rearrange, Move to…, Appearance…, Info…, More…, Delete.
- Appearance… = Rename, Card picture, Background picture, Title picture, Icon picture, Show as a full card / Show as an icon, Download new pictures. Info… = Details, About this game, Note, Copy its music. More… = Search games, Put on the panel button, Hide this card.
- A group with exactly one entry shows that entry in its place; an empty group is not shown; Move to… is exempt from promotion.
- Destinations come from one function, in `Settings::pageOrder` order; never the current page, never the Nintendo page (2), never a genre page; hidden pages included and shown again on move; a Nintendo-page card goes only to custom pages.
- Copy (verbatim): "Rearrange", "Move to…", "Appearance…", "Info…", "More…", "Add a copy to a page…", "New page…", "Hidden page", "Couldn't move {game}: that page isn't there any more.", "{game} moved to {page}" (existing row), "{game} is on the {page} page now." (existing).
- Timing (seconds): popup open 0.20, sheet open 0.24, level change 0.16, close fade 0.12; no popup open longer than 0.24. Settings > motion off = instant.
- Every new English string gets a `projects/cards/romfs/lang.tsv` row with all 9 languages (es pt ru fr de it ja zh tr); rows whose English left the code are deleted. `python3 tools/check_cards_lang.py` prints `ok: …`.
- No new dependencies. Both `SwitchU` (SWITCHU_MENU) and `CardsNX` targets build.

## Verification commands (used by every task)

- Host tests: `bash tests/run_cards_personalization.sh` → ends with `Widget model tests passed.` (needs `nlohmann/json.hpp` under `/opt/devkitpro/portlibs/switch/include` and libnx headers under `/opt/devkitpro/libnx`; both are installed in the cloud container).
- Strings: `python3 tools/check_cards_lang.py` → `ok: <n> rows, 9 languages`.
- Switch build (cloud container): `docker exec dkpw bash -lc 'cd /home/user/homenx-build && git fetch -q /home/user/homenx HEAD && git checkout -qf FETCH_HEAD && xmake build -y --root SwitchU && xmake build -y --root CardsNX'` → both end with `build ok`. (`dkpw` is the devkitPro container already configured for `/home/user/homenx-build`; the controller keeps it running.)

## Review Focus

1. A card on a custom page it was *moved* to, sent to Games: it must show on Games again and leave the custom page (not vanish from both).
2. A card that is only a *copy* on custom page A, moved from Games to custom page B: it stays a copy on A, and is moved (off Games) to B.
3. A destination deleted (or the page count changing) between opening Move to… and pressing A: no move, the failure message, settings unchanged.
4. B inside a level returns to the top level with that group's button chosen; Plus / X close from any level; touch outside the card closes from any level.
5. A sheet with 10+ buttons: every button fully visible once the open animation ends (today buttons 8+ never are).

Each has a test in the task that owns it (Tasks 1, 1, 4, 3, 5).

---

### Task 1: MoveTargets.hpp — the one destination list and move bookkeeping

**Files:**
- Create: `projects/cards/src/MoveTargets.hpp`
- Create: `tests/move_targets_test.cpp`
- Modify: `tests/run_cards_personalization.sh` (compile and run it, like `cards_personalization_test`)

**Interfaces:**
- Consumes: `cards::Settings`, `cards::CustomPage` (`Settings.hpp`: `pageOrder`, `hiddenPages`, `customPages`, `kBuiltinPages == 4`).
- Produces:
  - `namespace cards::moves { constexpr int kNintendoPage = 2; }`
  - `struct Target { int page; bool hidden; };`
  - `std::vector<Target> destinations(const Settings& s, int current, bool libraryMovable);`
  - `bool isDestination(const Settings& s, int current, bool libraryMovable, int page);`
  - `void applyCustomMove(std::vector<CustomPage>& pages, const std::string& id, int from, int to);` (page ids; custom index = page − `Settings::kBuiltinPages`; out-of-range pages ignored)

- [ ] **Step 1: Write the failing test** `tests/move_targets_test.cpp` (stub `cards::writeLater` exactly as `cards_personalization_test.cpp` does). Build a `Settings` with `customPages = {{"Faves",{},{},false},{"RPG",{},{},true},{"Kids",{},{},false}}`, `pageOrder = {3,0,4,1,2,5,6}`, `hiddenPages = {6}`. Assert:
  - `destinations(s, 0, true)` pages == `{3,4,1,6}`, and only page 6 has `hidden == true` (Nintendo 2, genre page 5 and current 0 excluded; order is pageOrder).
  - `destinations(s, 4, true)` == `{3,0,1,6}`.
  - `destinations(s, 2, false)` == `{4,6}` (Nintendo card: custom only).
  - `isDestination(s, 0, true, 4)`, `!isDestination(s, 0, true, 0)`, `!isDestination(s, 0, true, 5)`, `!isDestination(s, 0, true, 7)` (no such page).
  - `applyCustomMove`, id `"0100"`: (a) moved to 4 from 0 → page 4 `titles` and `moved` contain it once (call twice: still once); (b) then moved from 4 to 0 → absent from page 4 `titles` and `moved`; (c) copy on 6 (`titles` only) + moved on 4, moved from 0 to 4's sibling… concretely: page 6 titles={"0100"}, page 4 titles=moved={"0100"}, `applyCustomMove(p,"0100",0,6)` → page 4 has neither, page 6 titles has it once and moved has it; (d) a copy on page 6 only, move from 0 to 4 → page 6 still has it in `titles`, not in `moved`; (e) `from`/`to` beyond the custom pages or built-in → no crash, no change to other pages.

- [ ] **Step 2: Run, expect a compile failure** (`MoveTargets.hpp` missing): `bash tests/run_cards_personalization.sh`.

- [ ] **Step 3: Implement `MoveTargets.hpp`** (header-only, `inline` functions). Library pages are 0, 1, 3; custom pages are `kBuiltinPages .. kBuiltinPages + customPages.size() - 1` with `genre == false`. `applyCustomMove` order: remove from `from` (if custom: `titles` and `moved`); remove from every other custom page where it is in `moved` (both lists); if `to` is custom, add to `titles` and `moved` if absent.

- [ ] **Step 4: Run** `bash tests/run_cards_personalization.sh` → passes, including a new line `Move destination tests passed.`

- [ ] **Step 5: Commit** `git add projects/cards/src/MoveTargets.hpp tests/move_targets_test.cpp tests/run_cards_personalization.sh && git commit -m "CardsNX: one list of Move to destinations (MoveTargets.hpp) and a move's custom-page bookkeeping, tested"`

### Task 2: PlusMenu.hpp — what a card's `+` sheet shows

**Files:**
- Create: `projects/cards/src/PlusMenu.hpp`
- Create: `tests/plus_menu_test.cpp`
- Modify: `tests/run_cards_personalization.sh`

**Interfaces:**
- Produces (`namespace cards::plus`):
  - `enum class Entry : std::uint8_t { Play, Update, CloseSoftware, Favourite, Rearrange, MoveTo, Appearance, Info, More, Delete, Rename, CardPicture, BackgroundPicture, TitlePicture, IconPicture, FullCard, DownloadPictures, Details, AboutGame, Note, CopyMusic, SearchGames, PanelButton, Hide };`
  - `enum class Level : std::uint8_t { Top, MoveTo, Appearance, Info, More };`
  - `struct Card { bool get = false, installed = true, game = false, homebrew = false, hasId = true, hasUpdate = false, canClose = false, showDetails = true, canPanel = false, deletable = false; };`
  - `std::vector<Entry> entries(Level level, const Card& c);` — `Level::MoveTo` returns `{}` (its rows are built from `MoveTargets`).
  - `std::optional<Level> opens(Entry e);` — the level a group entry opens (`MoveTo/Appearance/Info/More`), else `nullopt`.

Conditions (from today's `openOptions`, `CardsActivity.cpp:4502-4715`): Update `hasUpdate`; CloseSoftware `canClose`; Favourite and Note `hasId && !get`; MoveTo `installed`; Rename `!get`; Card/Background/Title/Icon picture and Download new pictures `installed`; FullCard `installed && homebrew`; Details `showDetails`; AboutGame and CopyMusic `installed && game`; PanelButton `canPanel`; Hide `installed`; Delete `deletable`; Play, Rearrange, SearchGames always.

- [ ] **Step 1: Write the failing test** `tests/plus_menu_test.cpp` asserting:
  - Installed game `{game=true, deletable=true}`: Top == `{Play, Favourite, Rearrange, MoveTo, Appearance, Info, More, Delete}`; Appearance == `{Rename, CardPicture, BackgroundPicture, TitlePicture, IconPicture, DownloadPictures}`; Info == `{Details, AboutGame, Note, CopyMusic}`; More == `{SearchGames, Hide}`.
  - Same with `hasUpdate, canClose` → Top starts `{Play, Update, CloseSoftware, Favourite, …}` and has 10 entries (the most possible).
  - Homebrew `.nro` `{homebrew=true, deletable=true}`: Appearance contains `FullCard` between `IconPicture` and `DownloadPictures`; Info == `{Details, Note}`.
  - Get card `{get=true, installed=false}`: Top == `{Play, Rearrange, Details, SearchGames}` (Info and More each hold one entry → promoted; Appearance empty → gone; no MoveTo).
  - Get card with `showDetails=false`: Top == `{Play, Rearrange, SearchGames}`.
  - `opens(Entry::Appearance) == Level::Appearance`, `opens(Entry::MoveTo) == Level::MoveTo`, `!opens(Entry::Play)`.
  - For every combination of the 10 bools (1024 cases): Top size ≤ 10, `Play` first, `Delete` (when present) last, no group entry whose level is empty, and every entry of every non-Top level appears exactly once across Top ∪ its group (nothing lost).

- [ ] **Step 2: Run, expect compile failure.**
- [ ] **Step 3: Implement `PlusMenu.hpp`** (header-only). Top builds each group's entries, then: empty → skip; one entry and not MoveTo → that entry; else the group entry.
- [ ] **Step 4: Run** host tests → pass, new line `Plus menu model tests passed.`
- [ ] **Step 5: Commit** `"CardsNX: the + menu's entries as a tested model (PlusMenu.hpp): top level, Appearance, Info, More"`

### Task 3: The `+` sheet becomes two levels

**Files:**
- Modify: `projects/cards/src/CardsActivity.hpp` (option-sheet members near `m_optItems`, line ~667)
- Modify: `projects/cards/src/CardsActivity.cpp`: `openOptions` (4502-4715), `updateOptions` (5348), `optionsTouch` (5381), `drawOptions` (5401)
- Modify: `projects/cards/romfs/lang.tsv`

**Interfaces:**
- Consumes: Task 2 (`plus::entries`, `plus::opens`, `plus::Card`).
- Produces (members of `CardsActivity`):
  - `plus::Card plusCard(const Game& g) const;` — facts from today's conditions (`deletable` = either of today's two Delete conditions; `canClose` and `canPanel` false unless `SWITCHU_MENU`; `showDetails` = `!m_settings.xmbView || !m_settings.crossPlusDetails`; `hasUpdate` = `nroUpdate(g) != nullptr`).
  - `MenuItem plusItem(plus::Entry e, const Game& g);` — label, glyph, detail, value and `run` for one entry, each lifted unchanged from today's `openOptions`. Group entries: label "Move to…" / "Appearance…" / "Info…" / "More…", glyphs `"arrows"` / `"sun"` / `"dots"` / `"dots"`, `stay = true`, `run` = `showOptionLevel(*plus::opens(e))`. "Move" is relabelled "Rearrange".
  - `void showOptionLevel(plus::Level level);` — fills `m_optItems` for that level, sets `m_optLevel`, `m_optSel` (0 entering a level; the group's index when returning to Top).
  - `std::vector<MenuItem> moveLevelItems(const Game& g);` — for now today's movement entries unchanged (Move to {page} library moves, Move to custom page, Add to custom page, Remove from this page / Put back on its page). Task 4 replaces its body.
  - Members: `plus::Level m_optLevel = plus::Level::Top;`

- [ ] **Step 1: Refactor `openOptions`** to: guard as today, `m_optGame = g`, `showOptionLevel(Top)`, then the open animation/sound as today. Every entry's code moves into `plusItem`'s switch; nothing else changes behaviour.
- [ ] **Step 2: `updateOptions`**: A on an item with `stay` runs it without closing the sheet (sound `"activation"`); B on a non-Top level → `showOptionLevel(Top)` choosing the group that was open (sound `"navigation"`); B on Top, Plus, X close as today. `optionsTouch`: same `stay` rule; outside the card closes from any level.
- [ ] **Step 3: `drawOptions`**: on a non-Top level, the second line under the name shows the level's name ("Move to", "Appearance", "Info", "More") instead of the play-time line.
- [ ] **Step 4: lang.tsv**: add rows for "Rearrange", "Move to…", "Appearance…", "Info…", "More…", "Move to", "Appearance", "Info", "More" (9 languages each; check existing rows first — "Move" and some may exist); delete the row for "Move" only if its English left the code.
- [ ] **Step 5: Verify**: host tests pass; `check_cards_lang.py` ok; Switch build (both targets) `build ok`. Write the result lines into the report.
- [ ] **Step 6: Commit** `"CardsNX: + is two levels: Move to…, Appearance…, Info…, More… open in the same sheet, B goes back"` (body: list every top-level entry and where each old entry went).

### Task 4: Move to… — destinations from MoveTargets, one move path

**Files:**
- Modify: `projects/cards/src/CardsActivity.hpp` (`addToCustomPage` signature, new `moveCardTo`)
- Modify: `projects/cards/src/CardsActivity.cpp`: `moveLevelItems` (Task 3), `addToCustomPage` (14880-14907), `moveToPage` (2834) unchanged
- Modify: `projects/cards/romfs/lang.tsv`
- Modify: `tests/move_targets_test.cpp` only if a bookkeeping gap is found

**Interfaces:**
- Consumes: Task 1 (`moves::destinations`, `moves::isDestination`, `moves::applyCustomMove`), Task 3 (`moveLevelItems`, `showOptionLevel`).
- Produces:
  - `void moveCardTo(const Game& g, int from, int to);` — `from` is the page the sheet was opened on.
  - `void addToCustomPage(const Game& game);` — copy only (the `move` flag and its branch go); its list popup sets `m_menuBack` to reopen the sheet on the Move to level.
  - `static_assert(moves::kNintendoPage == kNintendo);` in CardsActivity.cpp.

- [ ] **Step 1: `moveLevelItems`**: one item per `moves::destinations(m_settings, m_page, libraryMovable)` where `libraryMovable` = the card is in `m_pages[kGames|kHomebrew|kMedia]` (not a Nintendo-page card); label = `pageName(page)`, detail "Hidden page" when hidden, glyph as today (`"play"` Games, `"dots"` Homebrew, `"music"` Media, `"copy"` custom); then "Add a copy to a page…" (`addToCustomPage(g)`, only when a non-genre custom page exists), then on a custom page "Remove from this page" / "Put back on its page" (today's code), then "New page…" (`openCustomPages()`).
- [ ] **Step 2: `moveCardTo`**: if `!moves::isDestination(m_settings, from, libraryMovable, to)` → `say(trf("Couldn't move {game}: that page isn't there any more.", {g.name}))`, return with nothing changed. Else under one `LibraryEdit`: un-hide `to`; `moves::applyCustomMove(m_settings.customPages, idText(g.id), from, to)`; library `to` → `moveToPage(g, to)` (it saves, speaks, plays); custom `to` → `m_settings.save()`, `rebuildCustomPages()`, `requeueArt()`, `m_audio.play("toggle_on")`, `say(trf("{game} moved to {page}", {g.name, pageName(to)}))`.
- [ ] **Step 3: Delete** the old movement entries' code and strings that left the code ("Move to {page}", "Move to custom page", "Off its own page and onto the custom one (deleting the page puts it back)", "A copy: it stays on its own page too", "Add to custom page" if unused, and the `move ? … :` texts). Add rows: "Add a copy to a page…", "New page…", "Hidden page", "Couldn't move {game}: that page isn't there any more." (9 languages, placeholders kept).
- [ ] **Step 4: Verify** host tests, lang check, Switch build (both). Walk Review Focus 1-3 against the code paths in the report (which lines make each hold).
- [ ] **Step 5: Commit** `"CardsNX: Move to… lists every page (custom ones too) from one function; one move path with feedback"`

### Task 5: MenuMotion.hpp — one set of timings, a stagger that always finishes, smooth level change

**Files:**
- Create: `projects/cards/src/MenuMotion.hpp`, `tests/menu_motion_test.cpp`
- Modify: `tests/run_cards_personalization.sh`
- Modify: `projects/cards/src/CardsActivity.cpp` (the `step(...)` list ~9978-10003; the "any animation running" list at ~1044; `drawOptions` button loop; `showOptionLevel`)
- Modify: `projects/cards/src/CardsActivity.hpp` (`float m_optLevelIn = 1.f;`)

**Interfaces:**
- Produces (`namespace cards::motion`): `constexpr float kPopupOpen = 0.20f, kSheetOpen = 0.24f, kLevelChange = 0.16f, kClose = 0.12f;` and `float stagger(float t, int i, int n);` — progress 0..1 of entry `i` of `n` at overall progress `t`; spread 0.3: delay = `n > 1 ? 0.3f * i / (n - 1) : 0`, result `clamp((t - delay) / 0.7f, 0, 1)`.

- [ ] **Step 1: Failing test**: for n in 1..30, i in 0..n-1: `stagger(1,i,n) == 1.f`, `stagger(0,i,n) == 0.f`; non-decreasing in t (step 0.01); `stagger(t,0,n) >= stagger(t,n-1,n)`; out-of-range t (−1, 2) clamps.
- [ ] **Step 2: Run, expect failure. Step 3: Implement.** Step 4: host tests pass (`Menu motion tests passed.`).
- [ ] **Step 5: Wire it**: `drawOptions` buttons use `stagger(min(m_optIn, m_optLevelIn), i, n)` in place of `easeOut((m_optIn - 0.04f * i) / 0.7f)` (eased with `easeOut`); `showOptionLevel` called while the sheet is open sets `m_optLevelIn = 0` (motion on) and leaves `m_optIn` alone, so the frame neither re-scales nor re-dims; step `m_optLevelIn` with `kLevelChange` and add it to the animation-running list. Replace the popup durations: `m_menuIn`, `m_taskIn`, `m_logIn`, `m_quickListIn` → `kPopupOpen`; `m_sheetIn`, `m_quickIn`, `m_powerIn`, `m_dotsIn`, `m_optIn`, `m_pickIn`, `m_musicIn`, `m_newsIn` → `kSheetOpen`. Others (`m_pageIn`, `m_detailIn`, `m_intro`, `m_sheen`, `m_pressT`, `m_bgFade`, `m_sheetRowsIn`) unchanged.
- [ ] **Step 6: Verify** host tests, Switch build. **Step 7: Commit** `"CardsNX: popups share one set of timings (MenuMotion.hpp); every + button finishes appearing; a level change re-staggers only the buttons"`

### Task 6: Closing fade and flicker-free hand-off

**Files:**
- Modify: `projects/cards/src/CardsActivity.hpp`, `CardsActivity.cpp` (`updateOptions`, `optionsTouch`, `drawOptions`, `openMenu` 4492, `updatePopup` 9196, `drawPopup` 14279, the step list)

**Interfaces:**
- Consumes: Task 5 constants.
- Produces: `float m_optOut = 0.f;` (1 when the sheet closes → 0 over `kClose`), `float m_menuOut = 0.f;`, `std::vector<MenuItem> m_menuGhost; std::string m_menuGhostTitle;` (what the closing list popup draws), `bool m_menuKeepDim = false;` (a popup opened from the sheet keeps the backdrop dimmed).

- [ ] **Step 1**: Closing the sheet (every path that sets `m_optOpen = false`) sets `m_optOut = 1` (motion on). `drawOptions` draws while `m_optOpen || m_optOut > 0`, alpha `m_optOpen ? easeOut(m_optIn) : easeOut(m_optOut)`; input never goes to the closing sheet (`updateOptions`/`optionsTouch` still gated on `m_optOpen`).
- [ ] **Step 2**: Same for the list popup: when `updatePopup` clears `m_menu`, it first moves items and title into `m_menuGhost*` and sets `m_menuOut = 1`; `drawPopup` draws the ghost fading; a new `openMenu` clears the ghost.
- [ ] **Step 3**: `openMenu` while `m_optOpen || m_optOut > 0` sets `m_menuKeepDim = true`: `drawPopup`'s backdrop draws at full dim from the first frame, and the sheet's own fade-out skips its backdrop that frame on (one dim layer, never zero, never double).
- [ ] **Step 4: Verify** host tests, Switch build. Note in the report each `m_optOpen = false` site touched (grep count before/after).
- [ ] **Step 5: Commit** `"CardsNX: the + sheet and popups fade out in 0.12 s without holding input; a popup opened from + keeps the backdrop dimmed (no flash)"`

### Task 7: Documentation and results

**Files:**
- Create: `docs/cards-plus-menu.md`

- [ ] **Step 1**: Write: the design (link the spec), the old → new map of every former `+` entry, Move to… semantics, timings, the test and build results from Tasks 1-6 (copied from the reports), a device checklist for crimson (Review Focus 1-5 plus: Nintendo-page applet to a custom page; hidden page reappears; motion off), and remaining issues (Phase 3 not started: needs a `perfLog` baseline on the console).
- [ ] **Step 2: Commit** `"docs: CardsNX + menu consolidation: what moved where, how Move to works, results"`

---

## Phase 3 (not in this plan)

Image / music / Discord / general loading needs a before-measurement on the console (Settings > perf log → `sdmc:/config/CardsNX/perf.log`). A separate spec and plan follow once crimson has sent that log from a build of this branch.
