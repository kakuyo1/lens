# Changelog

## Unreleased

### Added

- Online OCR now offers a service list instead of one fixed protocol: the settings page picks Baidu OCR, Tencent Cloud OCR, Alibaba Cloud OCR, Google Cloud Vision or OCR.space, the address defaults to the chosen service, and each service shows the credential fields it needs (Baidu, Tencent and Alibaba Cloud take a key pair). Each service keeps its own address and credentials, so switching saves the current ones and restores the target's own, while a language one service does not list falls back to one it does. The confirmation card names the service that will receive the crop. A failed test now says why in plain words: key or permission rejected, service not activated, quota used up, image refused, network unreachable, or service unavailable. An unclassified error code goes to the log. Baidu exchanges an OAuth token kept in memory only; Tencent signs every request with TC3-HMAC-SHA256; Alibaba Cloud signs an RPC query and posts the image bytes; Google sends one keyed JSON request. A new service is one adapter plus one provider-table row (docs/adr/0136).
- A word card's headword can be read aloud through a speaker glyph after the word, and the word alone: no sentence, entity, translation or breakdown is read. The Windows synthesiser speaks first through Qt's TextToSpeech module (WinRT's voices first, then SAPI when WinRT has no voice for the accent), then the dictionary's own recording, then a local model that is not downloadable yet. Settings gain a Pronunciation group for auto-play, accent, engine and fallback. A word's recording is cached under the Lens audio folder, 50 MB, and costs nothing; it never reaches the explanation service or the daily budget. The local model is tracked in TODO.md (docs/adr/0134, superseding docs/adr/0010).
- A **Mother tongue** setting above the explanation language, and the held peek it feeds: when the
  explanation language is English, the header of a word, sentence or entity bubble shows a globe that, held for 500 ms (a ring
  sweeps once), draws the meaning in the mother tongue under the definition until the hold
  ends. On a word, a Chinese mother tongue is answered by the pair the explanation already paid
  for; other mother tongues ask the new `peek` channel, once per word or text per session
  (docs/adr/0112, 0124). Hovering a
  mother-tongue dropdown row in the settings previews the English card with that row's peek line.

### Changed

- A word too long to leave room for its pronunciation beside it now takes the whole title line, and
  the pronunciation, the speaker and the part of speech go on a line under it. They used to claim
  the room and the word ended in an ellipsis, which hid the one thing the card was asked for.

- The model list first asks the standing service for its own shelf: one `GET {baseUrl}/models` sent
  with the reader's key. The shared catalogue is the fallback now, used when that answers nothing or
  when no key is set yet. The catalogue is the industry's list, so it kept names the standing vendor
  never opened at its own endpoint, and picking one of those was the 400 that said nothing about the
  model. A probe the service refuses with 400 now names the model it sent instead of calling the
  request malformed (docs/adr/0137).

- The liquid glass theme's **Text color** field is gone, and with it the stored setting and the
  colour kind behind it. It only took effect while the desktop under the glass was light enough to
  keep dark text; over a dark desktop the light-text table won and the field changed nothing. The
  liquid palette now carries its own `text` role, at the value the field used to start from.

- The **Word review** dropdown now says whose cadence it sets: a line under it reads *how often the
  pet raises its sign to ask a word you have looked up*, so the intervals are not mistaken for a
  timer on the reader's own studying.

- Account & sync is a category of its own in the settings list, sitting directly above About, rather
  than a row buried under General.

- RapidOCR's Chinese model glues Latin words together on a mixed page, so such a page is now read twice when that happens: the English model supplies the spacing for each line whose content matches the Chinese pass, with the Chinese characters put back in place. A line the second pass reads differently is left as it was, so nothing is guessed and no box is invented. Switching the recognition language also no longer asks the reader to trust the same installation again: the trust digest covers every model file Lens can use, so only an actual file change revokes it.

- The bubble's explanation-language shortcut is gone: no strip, no in-place switch, and the
  re-ask path behind it. Switching the explanation language permanently is a settings action
  again; the globe is the only language control left on the card (docs/adr/0112, superseding
  docs/adr/0067).
- In English, the sentence translate action restates the sentence in simpler English instead of
  explaining it again; explain is unchanged. The two used to send the same prompt, so they gave the
  same answer.

## 1.9.0 - 2026-10-25

The six surfaces have been through a full critique pass. Settings, the bubble, the tray menu, the
data panels, the selection bar and the update card now share a keyboard and screen-reader floor,
their quiet-state copy says what is actually happening, and the second pass closes the failure
paths that were still silent. The tray menu stays pointer-first by decision (docs/adr/0108).

### Added

- Traditional Chinese is now a fifth interface and explanation language, with a complete
  translation table, model prompt lines, and preview samples.
- A **Test connection** button on the API configuration page sends one minimal completion to check
  the address, key and model together, reports one fixed sentence, and never echoes the key
  (docs/adr/0110).
- Picture lookups now name why they failed (offline, no results, rate-limited, other), and the
  retry pill has the same quiet button face as the rest of the card.
- The statistics panel's Known and New rows open the words panel on that verdict, and the annual
  grid shows the selected day, its thresholds and an empty state per filter.
- The daily-budget page reads back today's spend, the cap and what reaching it does; an exhausted
  budget's tray row opens that page.
- Settings and the annual grid have a complete keyboard path: a visible scrollbar that follows
  focus, keyboard sliders and choices, named inputs and buttons, and arrow-key days. The selection
  bar, tray language list and words panel keep named screen-reader actions without taking focus
  (docs/adr/0107).

### Changed

- The review bubble in the words panel is placed against the word's own rectangle, keeps one
  window gap above it as it grows, and flips below when the top edge has no room.
- The wheel scrolls the settings, words and dropdown cards from any point, including the header,
  footer and scrollbar strip; the settings thumb follows the body instead of staying still.
- The selection bar's first arrival in a session keeps its full entrance; later arrivals take half
  the time (docs/adr/0109). Its at-rest forms now read as themselves: fan ribs and the tab's spine
  and fold are drawn one step brighter, the hand's middle card sits 2 px higher, the tag answers a
  press with its swing alone, and the settings preview keeps the hand's depth.
- The update card's checking state can be cancelled in place: Cancel replaces View in the same
  slot, checking runs a chunk at a time so the button can answer, and cancelling deletes the
  downloaded bytes. Its failure sentences now say the partial file was deleted.
- The tray menu marks an in-flight explanation with the same corner dot as the tray icon, and a
  left click opens the same menu as a right click.
- The reading page groups its seven card switches under quiet "Card content" and "Word details"
  labels; the model page explains tokens and the currency the cost panel shows; the connection page
  says where the key comes from; the hotkey box only names itself while recording; slider readouts
  use the mono face.

### Fixed

- Long collocations and hue names wrap instead of drawing past their card, the Defaults status
  stays readable, and a long API key is clipped to its field.
- A failed picture lookup is no longer just "unavailable", and the bubble's countdown holds for a
  screen reader's press while its one-time verdict hint is exposed as a description.
- The selection bar's tag no longer freezes half-faded after a press, and the tray menu's
  pointer-first contract is restored after its keyboard experiment was reverted.
- The desktop pet gives control back when a scene entry stalls, through a scene guard and a
  frame-clock watchdog.

## 1.8.5 - 2026-10-24

The desktop pet becomes more of a companion: every pet keeps its own wardrobe, size and home while
the window spot stays with the reader, scenes enter with a short choreography instead of a cut,
accessories tilt which scene the pet chooses, and the pet answers the pointer while hiding, follows
the review sign, and shows its mood in small gestures.

### Added

- Some accessories now have a temperament as well as a picture: a worn pair of frames makes the pet
  head for the desk or book nook more often, a scarf toward the rain, and so on. The pull is a
  multiplier on the scene lottery, so every scene keeps a chance and taking the piece off restores
  the old odds; the settings page names it in one faint line (docs/adr/0102).
- Every shipped pet now has its own nine-piece wardrobe: three head, three face and three body
  pieces, 63 in all, drawn for that pet's silhouette. The shared sets and the dog's seven-piece
  set are retired (docs/adr/0099).
- While a review question is up, the pet looks toward the sign beside it, turns to the answer card
  when it opens, and returns to centre once answered; only the direction travels, never the word
  (docs/adr/0097).
- The pet's mood now shows in three one-off gestures: a stretch on the idle turn after a long
  reading, a look around when it comes back from a stroll, and a stretch when it wakes from a nap
  or the reader resumes after a budget pause (docs/adr/0098).
- Scenes now begin with a short entrance instead of a hard cut: a hop into the bath, box or swing,
  a glide into a seat, or a fade for the ghost and cloud scenes; the landing splash or dust is a
  handful of QML pixels, and reduced motion still lands directly (docs/adr/0101).
- Each pet remembers its own size and accessories in the settings document; the window spot is one,
  the reader's, so switching pets no longer moves the window to another remembered place
  (docs/adr/0096, 0100).
- A pet hiding at a screen edge answers the pointer again: eyes follow a nearby cursor and the
  peek leans further in, a click on the visible sliver calls the pet out, and dropping a drag
  against an edge makes the pet tuck itself in (docs/adr/0095).
- The desktop pet enters from the nearest screen edge and looks around, hands over to a newly
  chosen pet from the same edge, and says goodbye with a heart before walking out. Turning
  animations off, in Windows or in the application, collapses the ceremony to an instant show or
  hide (docs/adr/0089).

## 1.8.0 - 2026-10-23

The About page gains a picture-cache line that shows what Lens has downloaded and clears it in
place. Word cards can now drill into the words their definitions use, sense pictures arrive with
the card, and the desktop pet brings its own scenes.

### Added

- Sense pictures: the word channel asks the model whether a sense is better shown with a photo and
  for an English query; opening that sense fetches three Wikimedia Commons thumbnails through the
  profile cache. A failed or empty lookup becomes an Open image search button, and the explanation
  never waits on it (docs/adr/0079).
- Definition chain: words in an English definition that rank above the band for the reader's level,
  or are not in the word list, draw a dotted line. Tapping one steps down inside the same card with
  a breadcrumb and a back arrow; each step is one explanation through the existing queue, cache and
  budget gate, and the trail stops at three (docs/adr/0078).
- The About page shows the downloaded picture cache's size and clears it in place. Only the
  thumbnails under `%APPDATA%\Lens\pictures\cache\v1\` go; the paid explanations and the review
  pool stay, and the next picture lookup downloads again.
- The desktop pet brings its own scenes: each shipped pet now owns two exclusive scenes plus two
  shared ones, and the dog is redrawn as a floppy-eared puppy. Action, accessory and scene ids are
  unchanged (docs/adr/0080).
- The update card can be dragged, and a running download draws its own bar with the bytes moved,
  the share done and the time left, read from the average pace rather than the digest list.

### Changed

- The review sign is now a small wooden sign standing beside the pet instead of a paper card held
  by a hand. The seven shipped pets no longer share one copied fist; a package without an `ask`
  pose still gets its sign, and a pet hidden at the screen edge no longer shifts inward to make
  room for it (docs/adr/0088).
- The card's IPA, signpost, collocations and picture blocks each have a display switch. They only
  change what is drawn: requests and the cache are the same, and turning one back on restores the
  block on the next cached card.

### Fixed

- Glass themes on a screen that refuses desktop duplication now follow the desktop through Windows
  Graphics Capture instead of falling back to the fifteen-a-second CPU read. On the measurement
  machine's internal panel the backdrop went from 6–10 to 95–130 refreshes a second, with the frame
  copy at about 1 ms (docs/adr/0087). The setting's note, and the sentence shown when no live source
  can serve a screen, now say the screen is read live rather than duplicated. Windows 10 keeps the
  GDI read, and a screen DXGI can duplicate keeps using it.
- A frame captured by Windows Graphics Capture is published only when its picture differs from the
  one the card already shows. Dragging the glass blur or refraction slider no longer competes with a
  full sheet upload and a second blur at the compositor's frame rate, which made the drag stutter on
  a screen served by capture.
- Glass themes blur in two separable passes, removing the diagonal weave the single-pass blur left
  at 125%.
- The fan, the tab's folded corner and the style previews are drawn with analytic antialiasing, so
  their diagonals no longer staircase at 125%.
- A breadcrumb on the word card no longer repeats the card's own title at the root.
- A filter change or a search no longer leaves either scrollbar's thumb mapped below its track.
- The backdrop refresh note in settings is drawn on the reader's theme instead of staying a flat
  panel.

## 1.7.5 - 2026-10-21

The desktop pet now wanders near where it was left and can hide at a screen edge, and the source tree
is regrouped around responsibilities. The regrouping changes nothing the reader sees.

### Added

- The desktop pet wanders after 120 to 300 seconds of accumulated idle time, staying within a short
  distance of the place the reader left it (10–60% of its own width; 24–46% for the shipped pets).
  Each of the seven shipped pets has its own gait: the dog trots, the cat pounces, the ghost,
  jellyfish and cloud float, and the mochi and owl hop. A package declares its gait and its `walk` /
  `edge_peek` actions in an optional `movement` block in `animations.json`; a package without one
  stays where it is (docs/adr/0076).
- A walk that ends against the left or right edge of the screen's work area has a 70% chance of
  hiding the pet. It slides out until about a third of its body shows, peeks back in every 3.6
  seconds, and stays there until the reader drags it back into the work area; clicking it does not
  end the hide.
- The pet can ask a review question while hidden, using the hand on the inside of the screen.
  Answering, letting the question time out, or closing it returns the pet to peeking, and the hide
  continues. Dragging the pet out ends the hide and leaves the question at the new position.

### Changed

- The C++ tree is grouped by responsibility: `src/core` separates domain rules, the settings
  document, crypto and the developer protocol, and `src/app` separates use cases, platform
  integration, glass, pet and the developer surface. The QML tree is grouped by surface family,
  shared components are split by role, and every theme table sits under `theme/`.
- `Main.qml` and `WordsPopup.qml` were split before reaching the QML size limit. Placement and panel
  routing moved to `SurfaceHost.qml`, and the words list row became a component of its own.
- The reference documents route through a single documentation map, and `CODING_STANDARDS.md` states
  the layer boundaries. A layer check runs in pre-commit and CI; neither changes behavior.

## 1.7.1 - 2026-10-20

The desktop pet stops drawing a black square behind itself, and the Background refresh rows stop
printing over their own labels.

### Fixed

- The pet, its review sign and the sign's answer card no longer paint a black square behind
  themselves. Each one's Win32 rules asked for the window handle while the window was still hidden,
  which created the platform window without the translucent presentation, so its transparent pixels
  composited as black (docs/adr/0075).
- A Background refresh row now says its tier's name only. The sentence that used to sit at its right
  end, where a row has no room for it and it overlapped the label, comes up beside the list when the
  pointer rests on the row, the way the theme, vocabulary and selection-bar previews do (UI.md 4.4).
- Pressing the field again puts the list down. The list's own window hangs over the lower part of the
  field, where Windows hit-tests the rectangle rather than the pixels and dropped the press, and the
  outside-press rule closed the list a beat before the field read whether it was up, so that press
  raised it back.

## 1.7.0 - 2026-10-20

Glass themes gain a Background refresh setting, and a duplicated screen's backdrop now follows that
screen's frames instead of a fixed clock.

### Added

- Glass themes gain a Background refresh setting under General: Automatic (the default), Battery saver,
  Standard and Smooth. Automatic follows a screen that can be duplicated up to about 60 times a second;
  Smooth follows that screen's refresh rate. Battery saver keeps the picture still and retakes it when
  the card appears or moves, and Standard stays on the CPU read at about 15 times a second. When the
  current screen cannot be duplicated, the row says the backdrop is reading at the standard rate
  (docs/adr/0073, docs/adr/0074).

### Changed

- A glass backdrop on a screen that can be duplicated now follows that screen's changes instead of the
  fixed 15-a-second loop, and a still backdrop copies nothing. Screens that refuse duplication keep the
  old CPU read; on the hybrid laptop here that is the internal panel (docs/adr/0072).

- Smooth now reads at the screen's refresh period and keeps a single live sheet's reads chained to the
  desktop's frames, instead of polling on a 16 ms timer. A duplicated frame is copied back with one GPU
  copy fewer and without a second full-image pass for the quarter turn, and it skips the fingerprint
  gate the dirty rectangles have already answered (docs/adr/0074).

### Fixed

- The Background refresh list closes on a second press and with the settings panel, instead of
  outliving the panel it belongs to.

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
- In Chinese, the built-in themes are named `晴朗白昼` and `静谧暗夜`.

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
