# FORK.md — Working on this fork

> Upstream now owns `AGENTS.md` (and `CLAUDE.md` is a symlink to it). This file is the
> fork-owned guide that used to live in `AGENTS.md`; `AGENTS.md` carries a one-line pointer here.

This repository is a **fork** of CrossPoint Reader, carrying custom features on top of an
actively-developed upstream. This file tells humans and AI agents how the fork is structured,
how we pull in upstream changes, and the rules that keep merges cheap.

Read this before adding features or running an upstream sync.

---

## 1. Fork topology

| | |
|---|---|
| **`origin`** | `github.com/kalleeh/crosspoint-reader` — our published fork |
| **`upstream`** | `github.com/crosspoint-reader/crosspoint-reader` — the project we track |
| **Working branch** | `my-fork` (pushed to `origin/my-fork`) |
| **Upstream branch we track** | `upstream/master` |

Our fork adds: arcade games, an AWS certification quiz/learning suite, online content
activities (Weather, Wikipedia, XKCD, History Today, Word of the Day), a fork settings
system, and assorted bug fixes.

---

## 2. Upstream sync mechanism: **MERGE, never rebase**

We integrate upstream by **merging `upstream/master` into `my-fork`**.

```bash
git fetch upstream
# ALWAYS do a non-destructive conflict check first:
git merge-tree --write-tree --name-only my-fork upstream/master
# Then, when ready:
git merge upstream/master
```

**Why merge and not rebase:** `my-fork` is published to `origin` and carries dozens of
fork-specific commits. Rebasing would rewrite shared, published history and force every
conflict to be replayed once per commit. Merging preserves history and resolves each
conflict exactly once. Precedent in history: commit
`Merge remote-tracking branch 'upstream/master' into my-fork`.

**Submodule:** upstream replaced the `open-x4-sdk` submodule with `freeink-sdk`
(`github.com/Free-Ink/freeink-sdk`, PR #2449). After a merge run `git submodule sync && git
submodule update --init freeink-sdk`. The old `open-x4-sdk/` checkout is no longer tracked and can be
deleted. The gitlink does NOT auto-conflict — always compare `git ls-tree HEAD freeink-sdk` with
`upstream/master` and take upstream's commit.

---

## 3. The golden rule: keep the fork surface tiny

Conflicts are caused by **one thing**: the fork and upstream editing the same lines of the
same file. Every rule below exists to minimize that overlap.

### 3.1 Prefer net-new files (zero conflict surface)

A file upstream never touches can never conflict. The overwhelming majority of the fork lives
in dedicated directories and is conflict-free by construction:

- `src/activities/games/` — all games
- `src/activities/learning/` — AWS cert suite
- `src/activities/online/` — online content activities
- `src/activities/home/AppsMenuActivity.*` — the fork's "Apps" hub
- `src/ForkSettings.*`, `src/customNavigation.*`, `src/QuizStatsManager.*`, `src/ReadingStatsManager.*`,
  `src/ForkLogging.cpp`, `src/DebugConfig.h`, `src/GameConstants.h`, `src/UIConstants.h` — fork infrastructure
- `src/activities/home/ReadingStatsActivity.*` — reading-stats screen
- `lib/I18n/ForkI18n*` — see 3.2
- `weather-images/` — assets

**When adding a feature: put it in a new file under one of these trees.** Touch shared files
only for the minimal wiring needed to reach the new code.

### 3.2 Fork strings live in the fork i18n system, NOT upstream YAML

Fork-specific translatable strings go in `lib/I18n/ForkI18n*` (`ForkI18nKeys.h`,
`ForkI18nStrings.cpp`) and are resolved with `fork_tr(...)`. **Never add fork strings to the
shared `lib/I18n/translations/*.yaml` files** — those are upstream-owned and editing them
guarantees conflicts on every release. This separation was built deliberately to eliminate
i18n merge conflicts; keep it intact.

### 3.3 Shared-file edits: minimal additive hooks only, marked `// FORK:`

When you genuinely must touch an upstream file, the only acceptable change is a **small,
additive, backward-compatible hook**, and **every hook line must carry a `// FORK:` comment**
so the whole divergence surface is greppable:

```bash
git grep "// FORK"   # lists every point the fork diverges from upstream
```

Acceptable hook patterns (these are why the core files merge cleanly today):

- **An include:** `#include "ForkSettings.h"  // FORK: fork settings`
- **An optional, nullptr-default callback** added to a constructor — existing upstream
  callers keep working unchanged.
- **A conditional insertion** guarded by the new capability:
  `if (onGamesOpen) { ... }  // FORK: Apps menu entry`
- **A config-gated behavior change:** `if (FORK_SETTINGS.showHiddenFiles) { ... }`

The `// FORK:` marker is also your conflict-resolution cheat sheet: during a merge, any line
marked `// FORK:` is **ours — preserve it**; everything else, take upstream's side and
re-apply the marked hooks on top.

### 3.4 Forbidden in shared upstream files (these cause conflicts for zero payoff)

Do **not** make any of these to an upstream-owned file. They produce guaranteed conflicts
with no feature value:

- **Cosmetic edits** — typo fixes in comments, reformatting, whitespace, renaming upstream
  identifiers (e.g. don't rename `errorOccured` → `errorOccurred`).
- **Refactors** — moving method bodies inline, reordering, reorganizing includes.
- **Version bumps** to upstream-managed constants (e.g. `BOOK_CACHE_VERSION`,
  `platformio.ini` version).
- **Debug noise** — stray `Serial.print*` / `LOG_*` lines left in upstream code.

If you spot one of these in the fork, **drop it during the next merge** (take upstream's
version of that hunk). They are the source of ~⅓ of current merge conflicts.

---

## 4. Fork Delta Registry

This is the durable, human-readable record of **every** intentional touch to a shared
upstream file — the useful core of "maintain patches," without the brittle auto-apply
tooling. (A flat `.patch` re-applied with `git apply` fails the same way a merge conflict
does, but with worse tooling and a second source of truth that drifts; a 3-way `git merge`
plus this registry is strictly better. So we document intent here and resolve via merge.)

**Keep this table current.** When you add/remove a shared-file hook, update the row. On every
upstream merge, walk this table to confirm each delta survived and is still placed correctly.

Status reflects the merge to `upstream/master` @ `aa994cf7` (release 1.6.0, 278 commits), completed
2026-09-06. Previous sync: `2754a5ff` (1.4.1) on 2026-06-29.

| Shared file | Fork delta | Category | Status / re-apply note |
|---|---|---|---|
| `AGENTS.md` / `CLAUDE.md` | fork guide | relocated | Upstream added its own `AGENTS.md` and made `CLAUDE.md` a symlink to it (add/add conflict). Fork guide moved to **`FORK.md`**; `AGENTS.md` keeps only a 2-line `> **FORK:**` pointer under the title. |
| `.gitignore` | fork asset ignores + `!.claude/skills/` | additive block | **Kept both** (upstream added sdkconfig/managed_components entries). |
| `src/main.cpp` | `#include "ForkSettings.h"`; `FORK_SETTINGS.loadFromFile()` after `SETTINGS.loadFromFile()` | hook (3.3) | **Re-applied.** Upstream moved the settings-load block; the `customNavigation.h` include was dropped (unused in main since the ActivityManager rewrite). |
| `src/activities/ActivityManager.h` | `HomeMenuItem::APPS` | hook (3.3) | **Re-applied** (auto-merge took upstream's enum silently — lesson 4.1 #1). |
| `src/activities/home/HomeActivity.{cpp,h}` | "Apps" hub menu entry | hook (3.3) | **Re-applied** on upstream's new `loop()` (`activateSelection` lambda + touch routing): count 4→5, `APPS` case, menu-item insert before Settings, `onAppsOpen()`. |
| `src/activities/boot_sleep/SleepActivity.{cpp,h}` | cached weather/word band (`renderInfoOverlay`) | hook (3.3) | **Re-applied.** Include conflict only; `.h` had to be re-hooked after taking upstream's. |
| `src/activities/reader/ReaderActivity.{cpp,h}` (was `EpubReaderActivity`) | reading-stats session clock + `recordSession` | hook (3.3) | **Moved.** Upstream extracted a `ReaderActivity` base (EPUB/TXT/XTC) owning `onEnter/onExit/render`; the stats hook now lives there. `statsSessionPageTurns++` stays in `EpubReaderActivity::pageTurn()`. |
| `src/activities/reader/EpubReaderActivity.cpp` | null-epub error screen | dropped (upstreamed) | `ReaderActivity::onEnter()` now `finish()`es when `loadBook()` fails — no blank screen, hook redundant. `STR_EPUB_LOAD_ERROR` is now unused. |
| `src/network/html/FilesPage.html` | multi-select delete; "Show hidden" toggle | upstreamed / dropped | **Took upstream.** Multi-select (`toggleSelectAll`, `.select-item`) is upstream now; hidden files are gated server-side by `SETTINGS.showHiddenFiles`. The fork's `renderFiles()` refactor was a forbidden restructure — dropped. |
| `lib/InflateReader/InflateReader.{cpp,h}` | caller-owned ring buffer for `init()` | dropped (upstreamed) | Upstream added `initWithRing()`/`RING_BYTES`, then moved PNG/EPUB inflate to the miniz-based `lib/miniz/src/InflateStream`. **Took upstream.** |
| `lib/PngToBmpConverter/PngToBmpConverter.{cpp,h}` | `pngFileTo1BitBmpStreamWithSize(..., bool crop = true)` | hook (3.3) | **Reduced** to the `crop` default param (XKCD needs fit-not-crop). The ring-buffer param is gone: XKCD now wraps the conversion in `GfxRenderer::FrameBufferLoan`, so `InflateStream` claims the 48KB framebuffer via `buildscratch::claim()` instead of the fragmented heap. |
| `lib/Epub/Epub.cpp` | `readSize == 0` guard | dropped | Upstream now streams the nav doc via `readItemContentsToStream`; the loop we patched no longer exists. **Took upstream.** |
| `lib/Logging/Logging.{cpp,h}` → `src/ForkLogging.cpp` | `MySerialImpl` definitions | hook, relocated | **Still load-bearing** (fork `DebugConfig.h` macros expand to `Serial.*`). Upstream marks `printf` deprecated; consider migrating fork logging to `LOG_*`. |
| `scripts/gen_i18n.py` | exclude `ForkStrId` keys | hook (3.3) | **Survived auto-merge** (`# FORK:` blocks at lines ~259 and ~896). |
| `src/activities/home/AppsMenuActivity.*`, `games/GamesMenuActivity.*`, `online/OnlineMenuActivity.*` (fork-owned) | list screens | n/a (fork file) | **Ported** from `GUI.drawList()` (removed in #2957/#3227) to upstream's `UiListActivity` base (`buildScreen`/`activateIndex`/`handleButtons`). Back stays press-edge via `handleButtons()` override. |
| `src/activities/learning/AWSCertMenuActivity.*` (fork-owned) | tab bar | n/a (fork file) | **Ported**: `GUI.drawTabBar`/`TabInfo` removed upstream; screen is a custom state machine so it draws its own 2-slot band (`drawTabBand`). |
| Games + `WeatherActivity.cpp` (fork-owned) | header battery | n/a (fork file) | `GUI.drawBatteryRight()` removed; with `showPercentage=false` it was identical to `drawBatteryLeft()` — renamed. |
| `src/activities/online/OnlineContentFetcher.h` (fork-owned) | `ensureWiFi()` | n/a (fork file) | **Ported**: `WifiCredentialStore` is now mutex-guarded and returns `std::optional` copies (`getCredentialCount/getCredentialAt/findCredential`). |
| `freeink-sdk` (submodule, replaces `open-x4-sdk`) | gitlink | **CRITICAL** | Upstream migrated SDKs (#2449). Merge staged the new gitlink; had to `git submodule sync && git submodule update --init freeink-sdk`. |

> Categories: **hook** = sanctioned additive change (3.3); **forbidden** = cosmetic/refactor/
> version/debug (3.4), drop on merge; **upstreamed** = the fork's feature is now in upstream.

### 4.1 Hard-won lessons (things the rules did NOT initially catch)

These cost real debugging on the 1.4.1 merge — check them explicitly next time:

1. **Auto-merged files can silently keep forbidden fork deltas.** `OpdsParser.h`, `Logging.h`,
   `Epub.h` merged without conflict markers but retained fork renames/refactors that broke
   against upstream's side. After taking upstream for a `.cpp`, **force upstream for its paired
   `.h` too.** Don't trust "no conflict" = "correct".
2. **Net-new fork files are conflict-free but NOT compile-proof.** `customNavigation.cpp` and
   the fork activities never conflicted, yet failed to build because they call upstream APIs
   that were removed/renamed (`enterNewActivity`, `FsFile`, `Serial`). **After any merge, a
   clean `git status` means nothing until `pio run` is green.**
3. **"Cosmetic" can be load-bearing.** The `Logging.cpp` `MySerialImpl::instance` definition
   looked like a refactor; it was the only thing making `Serial.*` link. When dropping a
   forbidden delta, grep for what depends on it first.
4. **The submodule gitlink does not auto-conflict.** A modify/modify on `open-x4-sdk` silently
   kept our side. **Always diff `git ls-tree HEAD open-x4-sdk` vs `upstream/master` and take
   upstream's SDK commit**, then fetch it (the submodule's local `origin` may point at the
   wrong remote — `.gitmodules` is authoritative).
5. **Upstream adds build-time gates.** The new `gen_i18n.py` validator broke the fork's
   separate-i18n strategy. Expect new `pre:`/`post:` scripts and `-Werror`-style checks each
   release; the fix belongs in a `# FORK:` hook, not by abandoning the separation.

Added on the 1.6.0 merge (2026-09-06):

6. **Upstream migrates UI primitives wholesale.** `GUI.drawList/drawTabBar/drawBatteryRight` vanished
   when lists moved to FreeInkUI. Fork screens that are plain lists should derive from
   `UiListActivity` (see the three menus); custom state-machine screens draw their own chrome.
   Check `grep -rhoE "GUI\.[a-zA-Z_]+\(" <fork dirs> | sort -u` against `BaseTheme.h` right after
   a merge — it finds these before the compiler does.
7. **Prefer upstream's memory mechanisms over fork hooks.** The XKCD ring-buffer hook became
   unnecessary once `FrameBufferLoan` + `buildscratch::claim()` existed; dropping the shared-file
   hook shrank the fork surface. Re-read `lib/Memory/BuildScratch.h` and `GfxRenderer.h` loan docs
   before re-applying any allocation hook.
8. **Scripted edits: anchor `str.index()` searches after the start marker.** A Python replace that
   searched for the end marker from the file start silently duplicated ~70 lines of a fork file.
   Always assert `end > start` and re-count a unique landmark after the edit.
9. **Same-name, different-class APIs.** `ctx.reader` in the PNG converter is now `InflateStream`,
   not `InflateReader`; `WifiCredentialStore` accessors return `std::optional`. Read the current
   type before porting a call.

---

## 5. Upstream sync checklist

1. `git fetch upstream` (and `git submodule sync` so URLs match `.gitmodules`).
2. Dry-run conflicts: `git merge-tree --write-tree --name-only my-fork upstream/master`
3. Cross-reference conflicted files against the **Fork Delta Registry** (§4).
4. `git merge upstream/master`.
5. Resolve conflicts by rule: **`// FORK:` lines are ours (keep); forbidden-category deltas
   (§3.4) get dropped in favor of upstream**; re-apply genuine hooks on upstream's structure.
6. **Audit auto-merged files (§4.1 #1):** for every shared file where you took upstream's
   `.cpp`, force upstream's paired `.h` too — auto-merge silently keeps fork deltas.
7. **Submodule (§4.1 #4):** compare `git ls-tree HEAD freeink-sdk` vs `upstream/master`; take
   upstream's commit, then `git submodule sync && git submodule update --init freeink-sdk`.
8. Watch for **renames/deletes** of files we hook (like `MyLibrary`→`FileBrowser`): re-apply
   the hook to the new file, don't resurrect the old one.
9. **Build is the gate (§4.1 #2):** `pio run -e default` must succeed (use
   `~/.platformio/penv/bin/pio` on this Mac; the Homebrew `pio` fails to bootstrap its venv) and the
   unit tests must pass: `cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release && cmake --build
   build/test -j8 && ctest --test-dir build/test --output-on-failure -j8`. A clean `git status` is NOT proof — net-new fork files can still call
   removed upstream APIs. Fix fork files that use migrated APIs (`FsFile`→`HalFile`, `Serial`
   defs, removed nav funcs).
10. Smoke-test fork features (Apps menu → games/online/learning), update the Fork Delta
    Registry (§4) for any moved hook, then push to `origin/my-fork`.
