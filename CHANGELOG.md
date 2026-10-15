# Changelog

## 1.4.0 - 2026-10-16

Chinese text comes back in English, and the desktop pet takes packages you install yourself.

### Added

- A selection or a screenshot region in Chinese is translated rather than explained. A word, a name and a
  whole passage all go to the same prompt, and the card draws the English and nothing else: no type label,
  no title, no pronunciation, origin or example. The translation is always English, whichever explanation
  language you have set. A short Chinese word raises the action bar too -- its minimum word length counts
  letters, and two Chinese characters are a whole word (docs/adr/0044).
- Chinese OCR, where you have it: put `chi_sim.traineddata` in the Tesseract data folder and screenshots
  read Chinese as well as English. The installer still carries the English data only.
- Pet packages, under Extensions. Put a data-only package (JSON and PNG, no code) in
  `%APPDATA%\Lens\pets\` and pick it from the list. An external pet is confirmed once before it is used,
  and a package whose files fail their checks is listed with the reason instead of quietly missing
  (docs/adr/0043, docs/pet-package-format.md).

### Fixed

- Dragging over a terminal selects again. Each terminal family has a copy combination its shell does not
  read as an interrupt -- Ctrl+Shift+C in Windows Terminal, Ctrl+Insert in the classic console host -- and
  those are sent instead of refusing the window by name.
- The action bar lands before it appears, instead of lighting up for a frame wherever it was last left.
- A selection retries its clipboard read. A single attempt could turn a copy that had just landed into
  "the clipboard changed but carried no text", which is where the empty selections came from.
- The account row in settings lines up with the rows under it, and the About page's footer sits at the
  bottom of the panel.

## 1.3.2 - 2026-10-15

Adds optional account sync. Sign in with GitHub and set a passphrase, and your learning data and selected
settings are kept in an encrypted secret gist in your own GitHub account. The project hosts nothing.

### Added

- Account sync, under General, then Account and sync. Sign-in uses GitHub's device flow and asks only for the
  gist scope. The passphrase encrypts the cloud copy. It is never stored; only a derived key stays on this
  device.
- What syncs: known and new marks, word history, daily statistics, and a list of settings such as appearance,
  bubble timing and explanation language. Explanations, the API key and the hotkey stay on the device.
- Sync runs five seconds after launch, after a change that alters what syncs, and every ten minutes while
  signed in. Automatic syncs that fail for a temporary reason retry with a backoff.
- Log out keeps this device's data. Delete cloud data removes the gist from your GitHub account.

### Notes

- GitHub needs a proxy from mainland China, as update checks do.
- A forgotten passphrase leaves the cloud copy unrecoverable. This device keeps its key, so it keeps syncing.

## 1.3.1 - 2026-10-14

Patch release: the year-record colour palette and the panel hand-overs move more smoothly, and a few
alignment and log fixes land.

### Changed

- The year-record colour palette unrolls below the graph, and the card grows with it. Closing runs the
  same motion in reverse. The second close button is gone: the open level shuts the palette again, and
  Esc still works.
- Panels hand over with a cross-fade instead of a cut. Going from the tray menu to statistics or settings
  no longer hides the menu first.
- A panel opened from the statistics stack (statistics, cost, words, year record) replaces the one it came
  from at the same bottom-right corner, and going back returns to that corner. A panel that grows holds
  that corner, and a panel you have dragged keeps the corner it was dragged to.

### Fixed

- The About page's version line is centred, and it rises when an update status line shows.
- The desktop pet's settings preview is centred on the figure rather than on its canvas, which sat about
  39 px to the right at 3x.
- The bubble, notice and other card windows no longer log "Unable to assign [undefined] to bool" when a
  card's timer starts.

## 1.3.0 - 2026-10-13

Minor release: the desktop pet gains three companions, the bubble gets timing and placement settings, and
the glass themes reach every window. The update check moves to a new About page.

### Added

- Desktop pets Mochi, Owl and Cat join the dog. Pick one in Settings, under Extensions.
- Bubble display time (1 to 30 seconds, 5 by default) and side (above or below the selection, above by
  default), in Settings under Capture & popups.
- An example sentence switch under Etymology, off by default. When on, each word explanation asks for one
  English example sentence, which costs output tokens.
- An About page, last in Settings: the current version, the repository link and the update check.
- Frosted glass and Liquid glass themes. With a glass theme on, every window that shows a card (settings,
  tray menu, stats, words, year words, cost, bubble, selection bar, notice, update and removal question)
  draws it over a blurred copy of the desktop behind it. Those windows are kept out of screen captures and
  screen sharing while the theme is on, so a screenshot shows the desktop without the cards.

### Changed

- The update check moved from General to the About page. General keeps the switch that checks at startup.
- Custom sits last in the theme list, and its colours are set with swatch rows.
- Selected chips use bold type.
- In Chinese, the built-in themes are named 晴朗白昼 (Light) and 静谧暗夜 (Dark).

### Removed

- The Forest theme. Light, Dark and Custom remain; a reader who had Forest selected is shown Light.

### Fixed

- On a portrait monitor above the primary, the bubble and action bar opened about a third of a screen below
  the selection, and a click on the action bar could close it. They now open beside the selection and stay
  inside the monitor's work area, clear of the taskbar.
- A dropdown list stays under its field while the page scrolls, and the pet page's dropdown closes on a
  press outside it.
- The version on the About page no longer jumps when an update check starts.

## 1.2.5 - 2026-10-11

Patch release: the Spanish and Japanese interfaces are translated in more places, and the internal
structure of the app's background duties is reorganised with no change to what the reader sees.

### Changed

- The Spanish and Japanese interfaces translate the update card, the update settings and the desktop
  pet's labels, which earlier releases showed in English. These translations are machine-generated
  and have not been reviewed by a native speaker.
- The app's background duties (capture, explanation, model catalogue and update) now expose their
  commands to the interface directly, and the word list draws its own time labels. Deleting a word no
  longer redraws the explanation bubble when that word is not the one on it.

### Fixed

- A model missing from the price list is reported once in the log, not on every call.

## 1.2.0 - 2026-10-11

Third release of Lens. A desktop companion arrives, off by default, and the app checks for new
releases and can download the installer once the reader asks for it.

### Added

- Desktop pet: a white dog that lives on the desktop and plays actions as you study. It is off by
  default and is switched on in Settings, under Extensions. It takes drags, clicks and three slots
  of accessories, with a mouse pass-through switch and a size from one to four times. It reacts to
  event types only and never reads your selections, explanations or screen text.
- The pet celebrates when a new version is offered on the update card.
- Update check: Lens asks the project's latest release once a day at startup, or when the
  reader presses "Check for updates" in General settings. A newer version raises a card with
  View and Skip this version; the startup check can be switched off there too.
- Download from the update card: the newer release's installer is downloaded, checked against
  the release's `SHA256SUMS`, and run with no arguments only when the reader presses Install.
  A release without a checksum list is not downloadable. Downloads need a proxy from mainland
  China; Lens says so when nothing arrives.

## 1.1.0 - 2026-10-09

Second release of Lens. Capture reaches past the selection to the screen itself, an explanation
reaches past one sense, and the tool gains its own themes, model selection and installer.

### Added

- Screen capture from a global hotkey, `Ctrl+Alt+S` by default: every screen is dimmed, the framed
  region is read with OCR, and its translation appears without a second question from the action bar.
- Automatic scanning: text that holds still in a whitelisted window is offered on its own.
- A capture page: switches for OCR and scanning, a two-to-five letter minimum word length, a process
  whitelist, and the OCR engine's own paths for a reader who has an installed copy.
- Several senses per word, up to three, behind the multi-sense switch.
- A word's origin beside its definition, fetched with it rather than in a second request.
- Spanish and Japanese explanations and interface, from the same language table as English and
  Chinese.
- A model service page, with the model field filtering the catalog's list as the reader types.
- A daily budget cap: when the day's spend reaches it, capture pauses rather than failing.
- Removing a word from the word list, and a year of lookups drawn as a contribution graph.
- Hovering a word in the list to read the explanation already stored for it.
- A Forest theme, reader-defined colours checked for contrast, and a switch for interface motion that
  follows the Windows reduced-motion preference.
- An installer whose pages carry the product's own colours and fonts, with the OCR runtime bundled.

### Changed

- The word list is a 168k common-English list rather than a word-frequency table.
- Providers, models, prices and prompts all come from `data/llm/`, so a new provider or language is a
  data change.
- The model list comes from one shared catalog request, asked for once per launch and sent without the
  reader's key or anything about what they are reading; the answer never overwrites the model in
  force.
- Helpers the modules had each grown their own copy of, and the logging code, now live in `src/util`,
  covered by offline tests.

### Fixed

- A long sentence no longer overflows the notice and leaves it unclosable.
- A connection that fails after an HTTP 200 is reported as a transport failure, not a schema error.

## 1.0.0 - 2026-10-05

First release of Lens, a Windows desktop English-learning tool that explains selected words,
entities, and sentences without opening a main window.

### Added

- Tray-resident interface with selection actions, explanation bubbles, settings, statistics, word
  history, and cost views.
- Word explanations with English, Chinese, IPA, caching, and learned/new-word marking.
- Entity explanations and sentence translation or explanation through separate LLM channels.
- Local selection filtering, redaction, response validation, and stale-response protection.
- Per-user settings storage under `%APPDATA%\Lens`, keeping API credentials out of the install tree
  and application logs.
- Windows installer built with Windeployqt and Inno Setup.

### Changed

- Selection classification now follows the shape of the selected text: a dictionary word, an entity,
  or a sentence.
- The project includes offline unit tests, Qt Quick component and surface tests, integration tests,
  performance checks, and a bounded real-model smoke test.

### Fixed

- Double-click and triple-click no longer open the selection action bar.
- Bubble placement no longer flashes during its first animation frame.
