# Changelog

## Unreleased

### Added

- Glass themes gain a Background refresh setting under General: Automatic (the default), Battery saver,
  Standard and Smooth. Automatic and Smooth follow a screen that can be duplicated up to about 60 times
  a second; Battery saver keeps the picture still and retakes it when the card appears or moves, and
  Standard stays on the CPU read at about 15 times a second. When the current screen cannot be
  duplicated, the row says the backdrop is reading at the standard rate (docs/adr/0073).

### Changed

- A glass backdrop on a screen that can be duplicated now follows that screen's changes instead of the
  fixed 15-a-second loop, and a still backdrop copies nothing. Screens that refuse duplication keep the
  old CPU read; on the hybrid laptop here that is the internal panel (docs/adr/0072).

## 1.6.5 - 2026-10-18

The bubble's text size is a setting of its own, word review can check every 5 to 15 minutes, and
closed windows give their graphics memory back.

### Added

- The explanation text has a size of its own: General › Font gains a bubble font-size dropdown from
  12 to 18 px, 13 px by default. It sets the body text; the word, pronunciation and example scale by
  the same multiple, so the card keeps its hierarchy at every size. Hovering a size previews it on
  the bubble beside the list.
- Word review gains an every-5-to-15-minutes level. The desktop pet checks for due words that often;
  the other levels keep their intervals.

### Changed

- On a still desktop with the settings panel open, the app uses about 10% of a core instead of 17%.
  The blurred backdrop behind a card is no longer redrawn when the picture under it has not changed;
  scrolling the page behind it still updates the backdrop.

### Fixed

- Closed windows give back their graphics memory and driver threads. Each window used to keep them
  until the app exited, so closing one returned none of it; a window hidden for half a second now
  releases them, and reopening it rebuilds them.

## 1.6.0 - 2026-10-18

The action bar takes the form the reader picks, and the explanation language can be changed from
the bubble itself. Three new companions join the desktop pet beside a rebuilt cat, and the settings
dropdowns now show what they offer before a value is chosen.

### Added

- The action bar has five forms: the classic bar (the default), a page-edge tab, three cards dealt
  above the selection, a hang tag on a string, and a fan of three wedges. The style is a setting
  under Capture & popups, and each option raises a hover preview of its shape like the other
  dropdowns. The three actions, the channel rules and the glass are shared by every form
  (docs/adr/0056).
- The explanation language can be changed from the bubble: a globe and the current language code
  sit left of the close cross, and the four catalog languages open in a strip on the card. Picking
  another language writes the global setting and asks again for the standing card -- from the
  cache when that language is already stored, otherwise from the model. The card keeps its
  position and height while the new answer writes itself out from the first character; a cached
  target is replayed rather than swapped in whole (docs/adr/0067).
- The desktop pet roster now includes the ghost, jelly and cloud, and the cat has been rebuilt
  with the final pixel-mask art. The four recipes ship 24 actions each and a shared nine-item
  wardrobe; each pet also has its own colour accent.
- An idle pet now notices the pointer near it: its eyes follow, it greets a nearby hold, and then
  plays one fuller reaction. The window stays the size of the sprite, and hovering only borrows
  idle time rather than interrupting the pet's rhythm.
- Settings dropdowns preview their choices: a theme row raises a miniature card drawn in that
  theme, a vocabulary level raises a word cloud for the band, and the font and explanation-language
  rows raise the explanation bubble in that face or language. The preview follows the open list and
  never writes the setting.

### Changed

- The API key saves when its field is left. The settings footer keeps only Defaults and the status
  line, which says Saved once the key is stored.
- Surfaces now fade out symmetrically instead of being cut away, and every icon button, switch,
  segment, chip and field shows press feedback. Deletion and selection got their missing
  transitions, and the year-in-words hue ring turns to the chosen hue following the prototype's
  wheel.

### Fixed

- Long Chinese and Japanese explanations now wrap between characters instead of running past the
  bubble's edge.
- Under the glass themes the bubble's own border no longer shows through; the glass rim is the only
  edge.
- Autostart follows the executable after the program moves, instead of pointing at the old path.
- The statistics panel refreshes when a cached word is counted by background work.
- Defaults restores the same values a fresh install has, while leaving the API key, autostart,
  interface language and daily budget untouched.

## 1.5.0 - 2026-10-17

### Fixed

- The review sign and its answer card now composite with per-pixel alpha on Windows, so the window
  background no longer paints black around the board.
- Hovering the answer card's buttons no longer closes the card before it can be pressed.
- Deleted words no longer come back after reopening the words panel or restarting, including across sync.

## 1.4.5 - 2026-10-17

English text wears a face of your choosing and word lists move in and out as files, while the desktop
pet lives its own life between clicks. The explanation is drawn as it arrives rather than after, and
every release now carries a portable zip beside the installer.

### Added

- English text has a face of its own: General › Font -- a subpage reached like Account & sync -- lets
  the reader set it from any family the machine can draw Latin with. The word, its pronunciation, the
  English definition, the example sentence and the word list follow it; everything in the reader's own
  language (the Chinese definition, the Spanish or Japanese translation, the dates, the interface)
  keeps the face the application installed, so this is one choice rather than one per language. The
  field is the typeable dropdown the model name uses, and a family this machine does not have is
  refused rather than stored: QML would draw it as whatever the font matcher picked, under a name no
  list shows. Hovering the page's title raises the explanation card in the chosen face, the same
  preview the five settings beside it have (UI.md 4.4). Picking a Latin face gives up the one-face
  pairing of Latin and Chinese that PRODUCT.md chose on 2026-10-03, for English runs only.

- Word lists can be brought in and taken out as files. The words panel's export dialog now offers
  JSON as well as text, and picks the format from the file name the reader typed; the JSON is a
  plain array of the lemmas, nothing else. A folder icon beside the export brings a list in: one
  word per line from a `.txt`, or that array from a `.json`. Imported words land on the list marked
  new, a word already on the list is not added twice, and repeats inside one file collapse.
- The dog has three more actions of its own to idle through -- wagging its tail, sniffing and
  scratching -- beside looking around and stretching. That brings it to five, the same as the cat,
  mochi and the owl, which already had five apiece; the dog had two and no further action to draw
  on at all.
- Right-clicking the desktop pet opens the settings panel on the page that carries its block
  (Extensions), placed beside the pet: the side is chosen from where the pet is at that click, with
  a gap, so the card never covers the pet. The tray's Settings entry is unchanged and still opens
  at the tray icon. With pass-through on no click reaches the pet window at all, so the gesture is
  off while it is set.
- The pace the explanation is written out at is a setting (General, beside Animations): a five-step
  slow-to-fast slider, one character every 80 ms at the slow end and 15 ms at the fast one, the
  shipped 35 ms in the middle. The blur follows the pace as a fixed ratio rather than sitting at a
  fixed 180 ms, so a slow speed leaves each character soft for longer without widening the blurred
  tail into a band of shaders (docs/adr/0046). The row is shown only while Animations is on: with
  motion off the answer arrives whole, so there is no pace to set.

### Changed

- Every release now carries a portable zip beside the installer: the same program, unzipped. A
  reader who would rather not install anything can run `lens.exe` straight out of the folder. The
  installer is still unsigned, and the READMEs now say so and say which two buttons get past the
  SmartScreen warning (docs/adr/0047).
- The desktop pet no longer lies down three minutes after the reader stops, and no longer stays
  down when it does. Idle is now a stream of the pet's own actions, and every turn draws whether to
  nap: one in twelve yawns and sleeps, for a minute and a half to five, and the nap ends by itself.
  The quiet counter that ran into the old fixed 180 seconds is gone, which also takes with it a
  yawn a random action at the same priority could swallow whole -- and the pet napping is now
  something the reader cannot predict rather than something they can time (docs/adr/0048).
- The idle pool is the pet's own list plus every one-shot action none of its roles names, so adding
  an action to a pet adds an idle action with it instead of needing a second list kept in step.
  `animations.json` and the package rules are unchanged, so no pet package needs rewriting
  (docs/pet-package-format.md).
- The explanation card is drawn while the answer is still arriving, instead of appearing once it is
  finished: it comes up on the first text and grows a field at a time, and it keeps following its
  word unless the reader is dragging it. Nothing is cached, counted or marked learned until the
  whole answer has passed the response check -- drawn in half, stored whole (docs/adr/0046).
- The desktop pet's size is a percentage, not four stops. The slider runs 0 to 100, where 0 is the
  old 1x and 100 the old 4x, so the same range is there in finer steps; a size chosen before the
  change carries over. Changing the size no longer moves the pet: it keeps its top-left corner and
  grows right and down, so one parked against a screen edge grows past it. Square sprite pixels are
  what the whole-number stops bought, and finer steps give that up (docs/adr/0045).

### Fixed

- The GitHub sign-in renews itself instead of dying eight hours after it is made. An OAuth app with
  expiring user tokens -- GitHub's default for a new one -- answers every gist call with 401 once the
  access token ages out; the refresh token is now kept and spent automatically, and the sync carries
  on from where the 401 stopped it. A refresh token that is itself refused leaves the sign-in
  labelled for what it is, "The GitHub sign-in expired; sign in again", rather than calling a dead
  sign-in an expired device code (docs/adr/0042).
- The text on a liquid-glass card holds its colour instead of flickering between the light and the
  dark tone. Over a mid-grey desktop the two tones read within 3% of each other -- the liquid theme
  reaches that at channel value 116 -- so one 8-bit level of the desktop sample flipped the text,
  and that sample is re-read about fifteen times a second. A tone now gives way only to a reading
  that wins by 15% and repeats (docs/adr/0035).

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
