# AGENTS.md — Working on this fork

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

**Known quirk:** the `open-x4-sdk` submodule can fail to fetch (`not our ref ...`) because a
pinned commit no longer exists on the SDK remote. This is unrelated to the main merge and
does not block it; address it separately if a build needs the submodule.

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
- `src/ForkSettings.*`, `src/customNavigation.*`, `src/QuizStatsManager.*`, `src/GameConstants.h`,
  `src/UIConstants.h` — fork infrastructure
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

Status reflects the merge to `upstream/master` @ `2754a5ff` (release 1.4.1), completed
2026-06-29.

| Shared file | Fork delta | Category | Status / re-apply note |
|---|---|---|---|
| `src/main.cpp` | `#include "ForkSettings.h"` + `"customNavigation.h"`; `FORK_SETTINGS.loadFromFile()` | hook (3.3) | **Resolved.** Upstream removed the old `onGoTo*`/`enterNewActivity` nav model entirely (now `ActivityManager`). Kept only the two includes + the settings load; the `onGoToApps` wiring moved into `HomeActivity` (see below). |
| `src/activities/home/HomeActivity.{cpp,h}` + `src/activities/ActivityManager.h` | "Apps" hub menu entry | hook (3.3) | **Re-architected.** Upstream replaced callback-injection with a `HomeMenuItem` enum + `ActivityManager`. Apps is now `HomeMenuItem::APPS` (added to the enum), wired in `getMenuItemCount`/`indexToMenuItem`/`menuItemToIndex`/`render`, dispatched in `loop()` to `onAppsOpen()` → `onGoToApps()`. All marked `// FORK:`. |
| `src/customNavigation.cpp` (fork-owned) | All games/online/learning navigation | n/a (fork file) | **Ported.** Was calling deleted `enterNewActivity`/`exitActivity`/`onGoHome` free funcs → rewritten to `activityManager.replaceActivity(std::make_unique<...>())` and `activityManager.goHome()`. ⚠️ *A fork-only file still breaks if it calls upstream APIs that were removed — net-new files are conflict-free but NOT compile-proof.* |
| `src/activities/home/FileBrowserActivity.cpp` | `showHiddenFiles` gate | upstreamed | **No action.** Was `MyLibraryActivity.cpp`; upstream renamed→`FileBrowserActivity` (PR #1260) AND already added `SETTINGS.showHiddenFiles`. Our feature is now upstream. Deleted the orphaned `MyLibraryActivity.cpp`. |
| `src/activities/boot_sleep/SleepActivity.{cpp,h}` | online-content sleep screens | hook (3.3) | **Resolved.** Both-add: kept our 3 `render*SleepScreen()` decls (marked) + upstream's `fromTimeout`. |
| `src/network/CrossPointWebServer.cpp` | client-side hidden-file filtering | upstreamed | **Took upstream.** Upstream added its own `SETTINGS.showHiddenFiles` gate in `scanFiles`. Fork rewrite redundant. |
| `src/network/html/FilesPage.html` | "Show/Hide hidden" toggle + multi-select delete | additive blocks | **Resolved** (CSS class conflict only — kept both `.delete-selected-btn` and upstream's `.delete-action-btn`). ⚠️ *Open follow-up:* fork's client-side dotfile filter now overlaps upstream's server-side gate — verify in the web UI. |
| `lib/Epub/Epub.cpp`, `Epub.h` | read-guard; include trims | mixed | **Took upstream.** Upstream independently added the same `readSize==0` guard; our `Epub.h` include-removal was a forbidden refactor — dropped. |
| `platformio.ini` | stale version bump + dup `PNGdec` | forbidden (3.4) | **Dropped — took upstream.** |
| `lib/OpdsParser/OpdsParser.{cpp,h}` | `errorOccured`→`errorOccurred` rename, etc. | forbidden (3.4) | **Dropped — took upstream both files.** ⚠️ *`.h` auto-merged and silently kept our rename → would not compile against upstream `.cpp`. Had to force upstream `.h`.* |
| `lib/Epub/Epub/BookMetadataCache.cpp` | stale cache version bump | forbidden (3.4) | **Dropped — took upstream.** |
| `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.h` | unused `htmlFileDir` member | dead code | **Dropped — took upstream** (member was never referenced). |
| `src/activities/reader/EpubReaderActivity.cpp` | null-epub error screen | hook (3.3) | **Re-applied** as `// FORK:` on upstream's `render()` (dropped the comment churn). |
| `src/activities/browser/OpdsBookBrowserActivity.cpp` | Back-cancel; debug line | dropped | **Took upstream.** Our "cancel" was a no-op pseudo-feature that fought upstream's deliberate input-block; debug `Serial.printf` dropped per 3.4. |
| `lib/Logging/Logging.{cpp,h}` → **`src/ForkLogging.cpp`** | `MySerialImpl` definitions | **hook, relocated** | **LOAD-BEARING — was misjudged as forbidden.** Upstream *declares* `MySerialImpl` but no longer *defines* it (fully migrated to `LOG_*`); fork code still uses `Serial.*`. Took upstream's `Logging.{cpp,h}`, then moved the definitions into a **new fork-owned `src/ForkLogging.cpp`** (zero upstream edit). |
| `scripts/gen_i18n.py` | exclude `ForkStrId` keys | hook (3.3) | **New hook.** Upstream added a build-time validator that aborts on any `STR_*` not in `english.yaml`; it didn't know about the fork's separate `ForkI18nKeys.h`. Added a `# FORK:` block that loads & excludes fork keys. |
| `src/QuizStatsManager.cpp`, `AWSCertQuizActivity.{cpp,h}`, online `*.cpp` (fork-owned) | file I/O type | n/a (fork file) | **Ported.** Upstream SDK migrated `FsFile` → `HalFile` (HAL wrapper) and dropped `readBytesUntil`. Replaced the type fork-wide; reimplemented the line reader with single-byte `read()`. |
| `open-x4-sdk` (submodule) | gitlink | **CRITICAL** | **Was silently kept at our old commit.** Upstream bumped the SDK to `198ad267`; the merge did NOT flag the gitlink as a conflict, so it kept ours — and the merged HAL code needs the new SDK API. Had to `git update-index` to upstream's commit, fix the submodule's wrong `origin` URL, fetch, and check out. |

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
7. **Submodule (§4.1 #4):** compare `git ls-tree HEAD open-x4-sdk` vs `upstream/master`; take
   upstream's commit (`git update-index --cacheinfo 160000,<sha>,open-x4-sdk`), then fetch &
   checkout it inside the submodule.
8. Watch for **renames/deletes** of files we hook (like `MyLibrary`→`FileBrowser`): re-apply
   the hook to the new file, don't resurrect the old one.
9. **Build is the gate (§4.1 #2):** `pio run -e default` must succeed and `pio run -t
   unit-tests` must pass. A clean `git status` is NOT proof — net-new fork files can still call
   removed upstream APIs. Fix fork files that use migrated APIs (`FsFile`→`HalFile`, `Serial`
   defs, removed nav funcs).
10. Smoke-test fork features (Apps menu → games/online/learning), update the Fork Delta
    Registry (§4) for any moved hook, then push to `origin/my-fork`.
