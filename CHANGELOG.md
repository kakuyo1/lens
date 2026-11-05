# Changelog

## 1.0.0-alpha - 2026-10-31

### Added

- The words list's review card and the pet's answer card carry a faint line saying when the pet might next ask about the word: the review curve's due time bucketed into an earliest-possible phrase (any time now, soon, around this time tomorrow, or in about N days, weeks or months). A check only takes the earliest due word and the checks themselves happen at random, so the line promises a lower bound, never a moment. It is left out when the review is off, and the hint for a word with no stored explanation never has one, since such a word is not in the pet's pool.
- Clicking **Translate** or **Explain** on the selection bar puts the card up in that same frame: the word the reader pointed at, the badge it already wears, and breathing lines where the answer will be. The card reserves the height that answer is expected to reach, so each streamed field fills space the surface already holds and nothing on it moves until the whole answer lands. The wait carries no countdown, no verdict and no peek globe, and the card comes down by itself when nothing is coming to fill it: a failed request, a word the reader has moved off, or a budget that never opened (docs/adr/0146).
- A first run without an API key shows one card that offers "Add API key", which opens the model page. It shows once; until a key is stored, the tray menu's first row reads "No API key yet" and opens the same page. The build switch `LENS_FORCE_ONBOARDING` shows the card on every launch for checking it (docs/adr/0144).
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

- A word's verdict is binary now: **Known** is the one verdict the reader gives, and every word
  not marked known is a new word. The words list fills the New pill on unmarked rows instead of
  leaving both plain (the card badge already read them that way), and the New filter holds every
  unmarked word. A pop stamps the verdict the word carries at that moment, so the statistics
  panel's Known and New rows add up to the day's lookups. The stored marks and the sync format
  are untouched: a `false` mark keeps its job as the vote that un-knows a word across devices
  (docs/adr/0147).
- The daily budget is no longer a settings category of its own: its field and today's spend sit at the foot of the model service page, below the API configuration. The tray's budget row opens that page; Defaults still leaves the budget as it is.
- **Auto scan** is marked Experimental in Settings and in every README, with a line under the switch saying it is still under development and its results are not reliable yet. The switch keeps working as an experiment; the label and the documentation no longer present it as a finished feature.

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
