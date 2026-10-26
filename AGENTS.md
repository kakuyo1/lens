## Role &amp; Purpose

You are the AI assistant for Lens, a Windows desktop English-learning tool built in C++ / QML (Qt 6).

## Critical Rules

- Minimize test time. Pick the row that matches the change, and run only what the row names:

  | Change | Run locally |
  |---|---|
  | Docs, comments, `.ts` text | Nothing |
  | C++ logic | One gtest case or `--gtest_filter` that covers the touched code |
  | QML that renders a visible surface | Drive it through the dev surface first (`scripts/qa/lens-dev.ps1`, `docs/qa.md`), plus the qtest case(s) covering the touched surface (`scripts/qa/qtest.ps1 -Target lens_qtest_surfaces -Case Settings::test_x`); a `ui-run` capture only when pixels are the question; no full-suite run |
  | Big merge, release, or cross-target change | The full run |
  | Anything else | Nothing; push and let CI run the suites |

  A review-comment fix never triggers a full run. Do not escalate "just to be safe". Report what you ran in one line.
- Reproducing a bug before fixing it is a judgement call, not a gate. Weigh what a reproduction would change against what it costs; when the fix is already clear, or when reproduction needs a human hand on the UI, fix directly and hand it to manual verification. A bug that would take a heavy repro rig to see again is usually not worth the rig -- skip the step.
- No commits unless explicitly requested, no AI attribution in commits or PRs.When the user said commit and push proactively, no test is needed, immediately commit and push and nothing else, this should be done under 30s.
- A branch that has been pushed gets a pull request, and its body follows
`.github/pull_request_template.md`: all three sections filled. Do not stop at handing over the
compare URL.
- UI work starts from `DESIGN.md` (the shared visual constraints) and the touched surface's `UI-SPEC.md` §4.x; it must comply with both and `PRODUCT.md`.
- Designing or auditing a `ui-prototypes/*.html` page starts by loading the `taste-skill` skill; a surface that has been rendered, the QML ones included, is checked with `visual-qa`, driven through the dev surface unless the question is pixels or the real hook path. Mouse injection and screenshots are the last resort, never the first look.
- Keep `GLOSSARY.md`, `PRODUCT.md`, `DESIGN.md`, and `UI-SPEC.md` in sync on any design change; `docs/README.md` owns which doc owns what; record major trade-offs in `docs/adr/`.
- Reply in the language the user writes in; Chinese by default.
- The API key lives in `%APPDATA%\Lens\settings.json`, the reader's own profile and never the install directory; it is never logged, never echoed in errors, never committed, and never copied out of the profile: read it in place. The repository's gitignored `settings.local.json` is that document's development source, copied over once on a first run (`src/app/main.cpp`).

## Project Structure

```
lens/
├── .claude           # project settings: the Qt skill family enabled (settings.json), and the run-lens skill
├── .clang-format     # code format spec
├── .githooks         # pre-commit and commit-msg; each file's header lists its checks
├── cmake             # headers only: the precompiled ones CMakeLists.txt applies
├── config            # paths.json — the machine paths; README.md owns the rule and its exemptions
├── data              # wordlist, llm/ (wire protocol as data), pet/, scenes/, fonts/, icons/, sync.json, update.json
├── docs              # README.md (doc routing), QML.md (QML facts), adr/ (trade-offs), metrics/, pet-*.md
├── i18n              # .ts translations; English is the source language
├── installer         # lens.iss — the Inno Setup script; version.iss.in, filled from project()
├── scripts           # build/, quality/, profiling/, qa/, data/, pet/, release/; see scripts/README.md
├── third_party       # vendored: nlohmann/json, spdlog, googletest
├── logs              # runtime logs, rotating, gitignored but for .gitkeep
├── src               # util/; core/ (domain/, document/, crypto/, dev/; no Qt);
│                     # llm/, sync/, update/, capture/; app/ (duties/, platform/, glass/,
│                     # pet/, geometry/, dev/, qml/, and the facade at its root)
├── test              # gtest (unit / integration / perf / smoke) and qtest targets, separate from src/
└── ui-prototypes/    # one HTML prototype per surface; see its README.md
```

## Build

Toolchain (verified): cmake 4.0.1 · Ninja 1.12.1 · MSVC 19.44

```
./scripts/build/build.bat                      # configure once, then incremental, into build-ninja
./scripts/build/build.bat --target lens_gtest_unit     # a test target, when you want one
./scripts/build/build-release.bat              # the same, into build-ninja-release (RelWithDebInfo)
powershell -File scripts/qa/ui-run.ps1         # run it; rebuilds the release tree first when stale
```

The default build is the application alone. Every test target carries `EXCLUDE_FROM_ALL` in `test/`,
so `all` does not pay for suites that change far less often than `src/` does, and each one is built
by naming it. CI names its own list, the four it runs plus `lens_gtest_perf`, so that gate is
unaffected. `TEST.md` section 2 says when each suite is worth running.

`build-ninja` stays Debug, which is where the assertions, the TRACE log and the PDB
`scripts/quality/coverage.sh` reads come from. `build-ninja-release` is the tree to run the application
from, and it exists as a second tree rather than a build-type switch because switching rewrites
every compile flag and rebuilds the lot. Both are driven by the same wrapper; the two environment
variables `scripts/build/build-release.bat` sets are the whole difference.

The last line runs the application rather than building it: it rebuilds `build-ninja-release` when its
`lens.exe` is older than `src/`, launches it with the repository as its working directory, and reports
the PID and `logs/lens.log`. The `run-lens` skill carries the driving and photographing around it.

The Qt prefix lives in `config/paths.json`, which `scripts/build/build.bat` exports as `QT_ROOT` before it drives
the presets. `config/README.md` owns the rule: machine paths come from that file, project paths stay
relative to the repository root, and nothing else writes either kind by hand.

## Test

Tests live under `test/googletest/` (gtest): `unit/` offline, `integration/` real Windows APIs (human-run), `perf/` measurement, `smoke/` real model. The Qt and QML side is `test/qtest/` (QTest), which needs a window; it is not here.

Set `QT_ROOT` from the `qtRoot` key of `config/paths.json` first; `build.bat` does that for itself.

```
PATH="$QT_ROOT/bin:$PATH" QT_FORCE_STDERR_LOGGING=1 \
  ./build-ninja/test/googletest/lens_gtest_unit.exe
```

`TEST.md` owns the targets, the corpus format and the run-record convention. Profiling and coverage methods live in `docs/metrics/profiling-and-coverage.md`; the drive-and-capture round lives in `docs/qa.md`.

Targets: `lens_util` → `lens_core` → `lens_capture` / `lens_llm` → `lens_app`. The `lens_gtest_*` targets are standalone and never shipped.

 ## Translations

English is the source language: `i18n/lens_en_US.ts` mirrors the source strings, and `lens_zh_CN.ts` / `lens_zh_TW.ts` / `lens_es_ES.ts` / `lens_ja_JP.ts` carry the Simplified Chinese, Traditional Chinese, Spanish and Japanese. After adding or changing a reader-facing string:

```
PATH="$QT_ROOT/bin:$PATH" lupdate src -ts i18n/lens_en_US.ts i18n/lens_zh_CN.ts i18n/lens_zh_TW.ts i18n/lens_es_ES.ts i18n/lens_ja_JP.ts
PATH="$QT_ROOT/bin:$PATH" lrelease i18n/lens_en_US.ts i18n/lens_zh_CN.ts i18n/lens_zh_TW.ts i18n/lens_es_ES.ts i18n/lens_ja_JP.ts
```

`lupdate` appends new strings as `unfinished` and leaves existing translations alone, so rerunning is safe. Write the Simplified Chinese into `lens_zh_CN.ts`, the Traditional Chinese into `lens_zh_TW.ts`, the Spanish into `lens_es_ES.ts` and the Japanese into `lens_ja_JP.ts`, copying the source text into `lens_en_US.ts`; `lrelease` is the check, and the goal is zero unfinished. All five reach zero before the commit, not after: `.githooks/pre-commit` refuses a staged `.ts` that still carries one, so the languages left untranslated are what blocks it. The interface language code `zh-Hant` maps to `lens_zh_TW.qm` in `src/app/main.cpp`; the explanation language uses the same catalog code, and its prompt line lives in `data/llm/request.*.json`. The `.qm` files are build output and stay gitignored.

Removing a string needs `lupdate -no-obsolete`: plain `lupdate` keeps the vanished entries in the file as `<translation type="vanished">`, so a deleted surface's strings go on sitting there with nothing to read them.

`lupdate` reads literal arguments only, so every `tr()` spells out its context and its string at the call site; routing them through a helper that takes the context as a parameter extracts nothing.

## Code Style

Comments are English and Doxygen-style. `///` with `@brief` on every declaration, unless the name alone states the whole contract; `@param`, `@return` and `@throws` only where they carry a constraint the name and the type do not -- a tag that restates the signature is left out. A `/** @file ... */` block at the top of each file. A rule is stated once: a `@note` or the prose adds to what the `@brief` said, never a second telling of it. Prose stays plain and as short as the facts allow: state the constraint and the reason, and stop -- no narrative framing, no rhetoric.

```cpp
/**
 * @brief Reduce a token to its dictionary form.
 * @param token Word to reduce, case-insensitive.
 * @return The lemma; the known set and the cache are keyed by lemma, not by the form written.
 * @throws std::logic_error If loadWordlist() has not run.
 */
std::string lemmatize(std::string_view token);
```

Log through the `LENS_TRACE` / `LENS_DEBUG` / `LENS_INFO` / `LENS_WARN` / `LENS_ERROR` / `LENS_CRITICAL` macros in `src/util/log.h`.

## Lint

`.githooks/pre-commit` is the source of truth for the C++ and documentation checks: the tools, their fallback paths and the per-doc typography budgets all live there, and it checks only the files a commit touches, so a hand run is what covers the rest.

`zhlint` 只用于仓库内供人类阅读的中文技术文档；不用于临时 handoff、agent-facing 文档或其他临时文件。

QML is the exception. `scripts/quality/qml-lint.sh`, called by the hook and by CI, owns its own argument list and its warning ratchet, because a .qml file only lints correctly beside its whole module.

## Documentation map

Start at `docs/README.md` for the full routing and the single-owner list. The short version:

| Task | Read |
|---|---|
| Product behavior or defaults | `PRODUCT.md` → `GLOSSARY.md` → `docs/adr/` |
| Surface look or interaction | `DESIGN.md` → `UI-SPEC.md` §4.x → `ui-prototypes/README.md` → `docs/QML.md` |
| QML implementation | `docs/QML.md` → `CODING_STANDARDS.md` |
| Wire format or validation | `API.md` → `data/llm/` → `SECURITY.md` |
| Tests | `TEST.md` → `docs/qa.md` → `docs/metrics/profiling-and-coverage.md` |
| Security or data boundaries | `SECURITY.md` → `PRODUCT.md` storage shape |
| Scripts, build or release | `scripts/README.md` → `config/README.md` |
| Why a decision was made | `docs/adr/README.md` → the matching ADR |
| Superseded specs | `docs/history/` |

Background: `README.md` (product overview), `CHANGELOG.md`, `TODO.md`, `CLAUDE.local.md` (this machine), and `docs/pet-*.md` (pet package and art).

## Agent skills

### Issue tracker

GitHub Issues in `kakuyo1/lens`, via the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Default five labels: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`. 