# Upstream review: ad3333m/homenx `untested`, 2026-11-12 → 2026-11-26

Reference commit `4b7eb80` (tip of upstream `untested`, 2026-11-26). Our fork split from
upstream at `63205f0` (2026-11-15), so the 14-day window holds 10 new upstream commits; the
commits from 11-12 to 11-15 are already in our history. Integration branch:
`upstream-integration`, based on `plus-menu-consolidation` at `e2b49f5`.
Recovery points: `backup/pre-upstream-2026-11-26` (= `2069457`, menu branch when this started); the menu branch itself is untouched.

## A. Commits reviewed

| Upstream | Date | What | Decision |
|---|---|---|---|
| bcf49ae | 11-17 | CardsNX status pill centred instead of right-aligned ("attempt") | Rejected: cosmetic, labelled an attempt, and the top bar is being reworked on the menu branch. |
| aa7ec35 | 11-17 | Daemon: game exit GetResult + sleep-step logging | **Taken** (daemon part, one change, see B2). |
| aa7ec35 | 11-17 | CardsNX: `select()` iterator "fix" | Rejected: `m_art` is a `std::map` here; insertion doesn't invalidate iterators. No bug in our code. |
| aa7ec35 | 11-17 | CardsNX: cache homebrew icons as the shown picture | Handed to the menu-loading work (same code, active branch). Likely part of the "reloads while scrolling" report for homebrew. |
| b64587f | 11-17 | Spotify replaced by Switube (YouTube Music) IPC client | Rejected: removes Spotify, which our fork extended (SwitifyClient); experimental; needs a separate sysmodule. |
| 94c5ebd / d343956 | 11-23 | Same tree twice. Native YouTube Music (FFmpeg, nestegg, libopus), video player, daemon media service, HOME launch/sleep/wake rewrite, custom VI layer, homebrew playtime, hbl-applet launch, Discord staged writes, TV 480p, exit-reason fix | Mostly rejected (see below). **Taken**: Discord save safety (rewritten, B3), TV 480p (B5), menu exit-reason fix (adapted, B1). |
| 5d8f6bc / d20a61c | 11-23 | Boot diagnostics (writer thread + watchdog in both processes, recovery flag, export tool) / merge | Deferred: ~1,000 lines on startup paths of both processes, built on d343956's lifecycle; doesn't apply to our tree and can't be judged without a console. Worth a separate port if black screens at boot recur. |
| 5c1a98f / 4b7eb80 | 11-24 / 11-26 | "Fix candidate" for upstream issue #20 (black screen after closing a game with HOME in CardsNX) | Deferred: it patches upstream's rewritten handoff (d343956), not our code; the issue has no logs and no confirmation that the fix works. |
| 189eb43 | 11-26 | YT Music sysmodule + Tesla overlay; homebrew start options (applet/application, memory pool npdm patch); loader handoff logging; streaming loader compare | **Taken**: streaming compare (B4). Rest rejected/deferred: music depends on the rejected stack; applet-mode homebrew depends on d343956's new daemon command and changes kernel capability/memory-pool bits that only a console can validate. |

### Why most of d343956 was rejected

- **Media stack** (FFmpeg, nestegg, libopus, `projects/media`, video player, daemon media service): new heavy dependencies and a separate sysmodule, replaces a feature we extended, streams from YouTube (ToS risk), and upstream itself calls it experimental (the message mentions "repeated daemon crashes").
- **HOME / sleep / wake rewrite** (daemon `main.cpp`, ~550 lines): it removes our wake-cover fix for the permanent black screen after a long sleep (#51), removes the early game launch (games start while the menu closes), and moves the HOME launch out of the foreground handler. These are behaviour changes on the riskiest path in the system, unverified upstream (`untested`), and not testable here.
- **Custom VI layer for the menu window** and the display-mode restart: hardware-only behaviour; a failure is a black screen at boot.
- **Homebrew playtime**: tied into the rewritten sleep/HOME flow.

## B. Integrated changes

| # | Commit | Files | Priority | From | Repairs vs upstream |
|---|---|---|---|---|---|
| B1 | d24b5ca | `projects/daemon/src/menu_launcher.hpp` | P0 | d343956 | Only the join-order fix; our terminate failure handling kept (upstream's depends on its rewrite). |
| B2 | 0b50c85 | `projects/daemon/src/app_manager.cpp`, `projects/daemon/src/main.cpp` | P0 diag | aa7ec35 | No forced SD commit right after `StartSleepSequence`. |
| B3 | c9eb2fb | `projects/common/include/switchu/safe_file.hpp` (new), `projects/menu/src/social/DiscordService.cpp`, `tests/safe_file_test.cpp`, `tests/run_cards_personalization.sh` | P0 data / P2 Discord | d343956 | Rewritten as a reusable, tested helper; reader prefers `.tmp` over `.bak` (upstream picked the older copy after a crash between renames); `hasSavedAccount` sees all copies. |
| B4 | a676325 | `projects/menu/src/launcher/AppletLauncher.cpp` | P0 memory | 189eb43 | Renamed; explicit `streamsize` cast. |
| B5 | 7450e23 | `projects/menu/src/settings/tabs/DisplayTab.cpp`, `romfs/i18n/*.json` | P1 | d343956 | Adds the `res_480p` string to all 9 languages. |

Bugs fixed: wrong menu exit reason in the daemon log (B1); Discord sign-in lost when a save is
interrupted (B3); `hbmenu.nro` read whole into HOME's memory on each homebrew launch (B4);
480p TVs shown and saved as 720p (B5). B2 adds diagnostics only.

## C. Testing

- Cloud build (devkitpro/devkita64 docker, xmake dev, `--homebrew=n --backend=deko3d` as in
  build.sh): `switchu-daemon`, `SwitchU`, `CardsNX` build OK, on 2069457 and again after the rebase
  onto e831900. No compiler warnings in any changed file (98 pre-existing warnings elsewhere).
  Not built here: HomeNX-Loader, managers, achievements overlay (none changed).
- Full `build.sh` (what build.bat runs in WSL: `JOBS=… XMAKE_ROOT=y bash ./build.sh`), run in the
  devkita64 container on this branch: all targets, achievements overlay, SD staging and
  HomeNX-SD-Ready.zip (61 MB, 586 files) succeed, exit 0.
- Host tests: `tests/run_cards_personalization.sh` (all 11 suites incl. the new safe-file test,
  ASan/UBSan) pass; `tools/check_cards_lang.py` ok (969 rows, 9 languages); all i18n JSON parses.
- Hardware tests: none (no console access).

Not verified (needs a console): every runtime behaviour above. In particular: B1 adds up to
0.5 s to a forced menu terminate if the applet doesn't end at once; B3's SD commits on real FAT;
B5 on a 480p TV.

## D. Applying

`git fetch <bundle> upstream-integration` then review `git log e2b49f5..upstream-integration`.
Each commit stands alone; drop any with `git rebase -i` (or cherry-pick the ones wanted onto
`plus-menu-consolidation`). None touch `CardsActivity.cpp`, so it rebases cleanly on new menu
commits.
