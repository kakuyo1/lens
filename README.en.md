<p align="center"><img src="docs/readme/brand/lens-mark.png" width="92" alt="Lens"></p>

<h1 align="center">Lens</h1>

<h3 align="center">Read English in English.</h3>

<p align="center">
  Lens is a Windows tray companion that explains the word, name, or sentence you select — in clearer English, right beside the text. No tab switch, no dictionary window, no translation detour. Between reads, a desktop pet brings the words that are due back and asks you for them.
</p>

<p align="center">
  <a href="https://github.com/kakuyo1/lens/releases/latest"><strong>Download for Windows</strong></a> ·
  <a href="#a-quick-look">See it in action</a> ·
  <a href="https://kakuyo1.github.io/lens/">Product tour</a> ·
  <a href="https://github.com/kakuyo1/lens/releases">All releases</a>
</p>

<p align="center">
  <a href="https://github.com/kakuyo1/lens/releases/latest"><img alt="release" src="https://img.shields.io/github/v/release/kakuyo1/lens?label=release&color=2f9e6e"></a>
  <a href="#download"><img alt="Windows 10 and 11, x64" src="https://img.shields.io/badge/Windows-10%20%7C%2011%20x64-1a1a1f"></a>
  <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/github/license/kakuyo1/lens?color=2f9e6e"></a>
  <a href="#languages"><img alt="English, Chinese, Traditional Chinese, Spanish and Japanese" src="https://img.shields.io/badge/languages-EN%20%7C%20ZH%20%7C%20ZH--HANT%20%7C%20ES%20%7C%20JA-2f9e6e"></a>
</p>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white">
  <img alt="Qt 6 / QML" src="https://img.shields.io/badge/Qt-6.2%2B%20%2F%20QML-41CD52?logo=qt&logoColor=white">
  <img alt="CMake 3.21+" src="https://img.shields.io/badge/CMake-3.21%2B-064F8C?logo=cmake&logoColor=white">
</p>

<p align="center"><sub>English · <a href="README.md">简体中文</a> · <a href="README.zh-TW.md">繁體中文</a> · <a href="README.ja.md">日本語</a> · <a href="README.es.md">Español</a></sub></p>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/readme/hero-dark.png">
    <img src="docs/readme/hero-light.png" width="100%" alt="Lens explains the word serendipity in English in a card beside an English essay">
  </picture>
</p>

<p align="center"><sub>Explanations default to English. Hold the globe on the card for your mother tongue; the permanent choice lives in Settings.</sub></p>

## Why Lens

Most lookup tools pull you out of the page and answer in your first language. You finish the paragraph without ever reading it in English. Lens makes the opposite bet: the fastest way to read better English is to stay in English.

- **English in, clearer English out.** A plain-English sense, IPA, and — when you want them — etymology and an example. Translation is still there; it just isn't the default.
- **A word you can hear.** A speaker sits beside every pronunciation: press it, or the pronunciation itself, and the headword is read once. Sentences, names, and translations stay silent.
- **The answer stays on the page.** Lens lives in the system tray and has no main window. The bubble opens beside the selection, holds while you hover, and is gone when you keep reading.
- **A quiet memory.** Mark a word Known or New. Lens keeps the list. No streaks, no lesson plan.
- **A pet that asks.** When a word comes due, a sign goes up at the pet's feet. Answer right and the next gap gets longer; answer wrong and it comes back sooner. The pet only receives event types and cannot read your screen, selection, or definitions. Off by default; turn it on in Settings › Extensions.
- **A definition you can walk into.** A word in a definition that sits above your level stays dotted. Open it and the same card steps down one layer, with a breadcrumb back; the cache, budget, and verdict rules follow every step.

## A quick look

<p align="center"><img src="docs/readme/english-first.gif" width="600" alt="Holding the globe on the card to show the Chinese meaning under the English definition"></p>

The answer is English. Hold the globe for your mother tongue, let go, and the card is English again.

<p align="center"><img src="docs/readme/feature-tour.gif" width="600" alt="Pressing Pictures on the word crane loads its photos; Not the Sense? swaps the card to the machine sense, photos and all"></p>

Press Pictures on a word to load its photos. Not the Sense? swaps the card in place to the word's other sense, photos and all.

## English first. Translation when you ask.

- **Explanations default to English.** A selection in Chinese, Japanese, or Spanish comes back in English.
- **The explanation language is a setting, not a switch on the card.** English, Chinese, Traditional Chinese, Spanish, or Japanese — change it once and every card follows, answered from cache when that word already has one.
- **The action bar offers Translate, Explain, and Copy text.** Explain is the one Lens is built around: it tells you what the word means in words you already know, instead of replacing it with a Chinese word you can skip.
- **Translate is the exception.** It turns a Chinese, Japanese, or Spanish passage into English; on English text it answers in your explanation language — English unless you change it.
- **The globe is the language control that stayed on the card.** Hold it for half a second and the meaning in your mother tongue appears under the English definition; let go and it is gone. The mother tongue is a setting of its own — Chinese by default. A word whose explanation already includes that language costs nothing to peek at; any other is asked once per session.

## Say it out loud

Every word card carries a speaker beside its pronunciation; pressing either one reads the headword once. Nothing else on the card is spoken — not the definition, not a sentence, not a name.

- **The Windows voice speaks first**, in the accent you pick: American by default, British on request.
- **When the machine has no voice for that accent**, Lens falls back to the one you chose in Settings › Reading & learning: an online recording (Youdao by default, or Google Translate), a local model you download once (Kokoro, about 180 MB, or LJSpeech, about 41 MB), or silence. The choice is explicit — Lens does not quietly move between online and local.
- **Auto-play is off by default.** When it is on, the card a lookup opens reads once, and the same word does not repeat.
- **A recording is cached under `%APPDATA%\Lens\audio`**, capped at 50 MB, and never counts against the daily budget.

## Text you can't select

Press `Ctrl+Alt+S` (changeable in Settings), frame a region of a PDF, image, or video still, and Lens reads it locally with OCR by default.

- **The bundled Tesseract is the automatic engine.** A reader can install RapidOCR-json, choose it explicitly, and point it at an ASCII model folder after an explicit trust check.
- **Online OCR is opt-in.** It asks before each upload and can be pointed at Baidu OCR, Tencent Cloud OCR, Alibaba Cloud OCR, Google Cloud Vision, or OCR.space.
- **Automatic scanning is still experimental and under development.** It watches only the apps you whitelist, and its results are not reliable yet.

## Five action bars, one set of actions

<p align="center"><img src="docs/readme/selection-styles.gif" width="800" alt="Classic bar, side tab, hand of cards, hang tag and fan selection styles"></p>

Choose the shape in Settings › Capture & popups: the classic bar, a page-edge tab, a hand of cards, a hang tag, or a fan. The three actions and channel rules stay the same, and the copy drawer follows the shape it opens from.

<p align="center"><img src="docs/readme/selection-drawer.png" width="800" alt="The copy text's drawer listing Fix syntax and Breakdown for a selected sentence"></p>

Copy text is a drawer, not a single action: hover it and only the actions this selection can take come out.

- Fix spelling on a word, Fix syntax on a sentence, and Breakdown on an English sentence of up to 80 words.
- Corrections and breakdowns are not cached, and each counts toward the daily budget like any other request.

<p align="center"><img src="docs/readme/breakdown.png" width="800" alt="The breakdown answer: the sentence tinted in its four role colours, with a legend under it"></p>

Breakdown tints each part of the sentence in one of four colours — subject, verb, complement, adverbial — with a legend under it; a correction underlines only the words it changed.

## A small learning loop

<p align="center"><img src="docs/readme/learning-loop.gif" width="900" alt="The ghost's review sign beside it, and the year-in-words graph"></p>

Known and New live on one word list.

- Hover a row to reread an explanation Lens already stored, without sending a request.
- When a word is due, a small sign appears beside the desktop pet — its own, drawn for that pet; answer Forgot or Remembered and it comes back on a spaced schedule from ten minutes to sixty days.
- The year-in-words graph keeps the habit visible without turning it into a streak.

## Pets that ask you the words back

<p align="center"><img src="docs/readme/pets.gif" width="900" alt="Lens's seven pixel pets"></p>

Seven pixel companions ship with Lens: Stub cat, Owl, Mochi, Ghost, Jellyfish, Cloud, and Dog. They are off by default; turn one on in Settings › Extensions.

- Each pet has its own nine-piece wardrobe and six scenes — four of its own and two shared; a scarf or a pair of frames can tilt which scene it chooses.
- Its size and outfit are remembered per pet, while the window stays where you put it.
- They wander, hide at the screen edge, notice the pointer, and can be picked up and thrown.
- They react to event types only — never to your selections, explanations, or screen text — and packages hold data only.

The ghost's six: a midnight clock, a jack-o'-lantern, a mirror and a tea party of its own, and the desk and the bubble bath it shares with the other pets — each scene an animation of its own. The jack-o'-lantern, playing:

<p align="center"><img src="docs/readme/pet-lantern.gif" width="420" alt="The ghost's jack-o'-lantern scene, animated"></p>

And the card the settings raise: hover a pet in Settings › Extensions and its postcard comes up — dressed in one piece per slot, standing in a landscape of its own, stamped and postmarked.

<p align="center"><img src="docs/readme/pet-postcards.gif" width="700" alt="The seven pets' postcards, one after another"></p>

## Also in the box

- **Bring your own model.** DeepSeek, OpenAI, Gemini, OpenRouter, xAI, Moonshot, Zhipu, Qwen, or a custom OpenAI-compatible endpoint.
- **Costs you can see.** Per-request cost from tokens, and a daily budget that pauses lookups, capture, and requests when the day reaches it.
- **Sync when you want it.** Word marks, lookup history, statistics, and selected settings are encrypted with your passphrase and kept in a secret gist in your own GitHub account. The API key stays on this device.
- **Word lists in and out.** Import or export `.txt` and `.json`; delete a word with one confirmation.
- **Pictures when they help.** A sense the model thinks a photo explains better can open three Wikimedia Commons pictures. They are cached locally, and About shows the cache size and clears it in place.
- **Make it yours.** Five action-bar shapes; Light, Dark, Frosted glass, Liquid glass, and Custom themes; and an English typeface with 12–18 px bubble text.
- **Five languages.** Interface and explanations each offer English, Chinese, Traditional Chinese, Spanish, and Japanese. The Traditional Chinese, Spanish, and Japanese translations are machine-generated and have not been reviewed by a native speaker.

## Themes

<p align="center"><img src="docs/readme/themes.png" width="900" alt="Light, Dark, Frosted glass and Liquid glass"></p>

- **Light and Dark** are solid.
- **Frosted glass and Liquid glass** draw every card over a blurred copy of the desktop, and keep Lens out of screenshots and screen sharing while they are on.
- **Custom** lets you choose the colours and checks them against WCAG AA.
- **Animations** follow the Windows reduced-motion setting.

## Download

Download `Lens-<version>-setup.exe` from the [releases page](https://github.com/kakuyo1/lens/releases/latest), or `Lens-<version>-portable.zip` from the same release.

- **The installer** is per user, so Windows never asks for administrator rights; **the portable zip** runs from any folder.
- **Windows 10/11 x64.** You will need an API key from a supported provider.
- Lens does not ship a definition dictionary; it includes a list of about 168,000 English words only to check that a candidate is a real word.

<details>
<summary>Windows may warn you before it runs</summary>

Lens is not code-signed, and SmartScreen blocks programs it does not recognise. Choose **More info**, then **Run anyway**. The warning is about the missing signature, not a damaged file. To check that the download arrived intact, compare its SHA-256 with the matching line in the release's `SHA256SUMS` file; that proves the file was not altered in transit, not who published it.

</details>

## Privacy

Lens has no account and no server of its own.

- **What is sent.** Only the text you select or capture is sent to the provider you configure, after local masking of URLs, email addresses, and digit strings of six or more digits.
- **OCR.** It runs locally by default. Online OCR is opt-in: each capture shows the frozen crop and the exact destination, uploads only after you confirm, and experimental automatic scanning never uses it.
- **The speaker.** It reads on this device through the system voice or a downloaded local model; when it falls back to an online recording, only that word leaves the device, and the recording is cached under `%APPDATA%\Lens\audio`.
- **On this device.** Your explanation key, OCR credentials, and learning data stay in `%APPDATA%\Lens\settings.json`.
- **Sync.** If you turn it on, the cloud copy is encrypted with your passphrase and stored in your own GitHub gist; the keys stay on this device.

See [SECURITY.md](SECURITY.md) for the full boundary.

## FAQ

**Is Lens a translator?** It can translate, but it is built for explanations. Chinese, Japanese, and Spanish selections come back in English.

**Do I need an API key?** Yes. Bring one from a supported provider. Lens shows the cost of each request and can cap daily spending.

**Does it work offline?** Local OCR, the word list, and a local voice you have downloaded work offline. Explanations, and online OCR or online recordings if you enable them, need the service you configure.

**What leaves my PC?** Only these: the masked selection, a crop you explicitly confirm if you use online OCR, and the single word when the speaker falls back to an online recording. See [Privacy](#privacy).

**Why does Windows warn me?** The installer is not code-signed; the note under [Download](#download) explains how to run it and verify the file.

**Is there a macOS or Linux build?** Windows only for now.

## Languages

[English](README.en.md) · [简体中文](README.md) · [繁體中文](README.zh-TW.md) · [日本語](README.ja.md) · [Español](README.es.md)

The interface language and the explanation language are separate settings. Both offer English, Chinese, Traditional Chinese, Spanish, and Japanese.

## Contributing and license

Lens is open source under the [MIT License](LICENSE). Issues and pull requests are welcome. If Lens helps you read more English, a star helps another reader find it.

<sub>Interface images are rendered from Lens's current design prototypes; the shipped build follows the same layout and tokens.</sub>
