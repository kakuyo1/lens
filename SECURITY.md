# Security

> Authority: the current data and trust boundaries. Decisions live in `docs/adr/`.

## Reporting a vulnerability

Use GitHub's private vulnerability reporting if it is enabled. Otherwise, open a public issue asking for a private channel, without exploit details or real keys. There is no response-time commitment, and older releases do not get fixes.

## Outbound requests

| Destination | What is sent | Notes |
| --- | --- | --- |
| AI provider you configure | The word, entity, or sentence being explained. Also the weekly profile summary or story words, when you press those buttons. | HTTPS. The provider's retention and training terms apply. |
| OCR service (online OCR only) | One screenshot crop you confirm, per capture. | Never automatic. Local OCR is the default, and automatic scanning is always local. |
| Youdao or Google Translate | The single word, for pronunciation. | Only when online audio is the active engine. No key is sent. |
| Wikimedia Commons | A 2–5 word English query. | Only when you open pictures. Nothing else is sent. |
| GitHub | Sign-in, encrypted sync, the update check, and downloads you start. | The update check sends only a version User-Agent. GitHub sees the gist's size, update times, and access IPs, not its contents. |
| huggingface.co | Voice-file downloads. | No reader data. |

## Local data

- `%APPDATA%\Lens\settings.json` holds your settings, learning data, caches, the API key, OCR credentials, the GitHub token and its refresh token, and the sync key. It is plain JSON that Lens does not encrypt. Anyone who can read it can decrypt the cloud copy.
- Pictures, pronunciation audio, and voice models are cached locally. None of them is synced or uploaded.
- Logs can contain the words you looked up. Logs rotate, and for packaged launches they may be written inside the install folder.
- Selection capture briefly replaces the clipboard and then tries to restore it. Another application can prevent a perfect restore.
- Pet packages are read from `%APPDATA%\Lens\pets\` as data. Lens runs no code from them.

## Safeguards

- Lens classifies text locally and sends only what a request needs. It never sends a full-screen image to the explanation API.
- Before sending, URLs, email addresses, and digit strings of six or more are masked. This is pattern masking, not full personal-data detection.
- The API key stays in your per-user profile, not the install folder. Lens does not log it on purpose.
- The sync passphrase is never stored. The key is derived with PBKDF2-HMAC-SHA256 at 600000 iterations and used with AES-256-GCM. Explanation caches are never uploaded.
- Model answers stream onto the card before the full check finishes. Nothing is cached, counted, or marked until the whole answer passes validation.
- An update installer runs only after you press Install, and only if its SHA-256 matches `SHA256SUMS`. That checksum shows the file arrived intact, not who published it. The installer is not code-signed.
- A pet package is refused whole if any check fails: a path that escapes the package, a non-PNG file, an oversized image, or an index out of range.
- The desktop pet reads the cursor position only to react to it. It reads no pixels or text.
- Picture lookups use HTTPS, are capped at 2 MiB and 4 seconds, reach only two Wikimedia hosts, and do not follow redirects.

## OCR contract

- Online OCR sends a crop only after you confirm that crop and its destination. Changing the service, endpoint, language, or credentials cancels a pending confirmation.
- Online OCR credentials sit in two slots, separate from the explanation key. Each service keeps its own, and no service's credentials go to another service.
- Crops and raw responses stay in memory for the transaction, at most 120 seconds. A failed transport allows one manual retry. There is no automatic retry and no fallback to another provider.
- RapidOCR is a third-party executable: a pinned download, or one you install. It runs with your privileges and is not sandboxed. Lens uses it only after explicit detection, and any change to the program or its model files revokes that trust.
- Live online OCR and its billing have not been verified against real services.

## Reader guidance

- Do not send anything you would not show your provider.
- Read the provider's privacy terms before you add a key. A custom endpoint receives both the key and your content, so use only one you trust.
- Leave automatic scanning off unless you need it, and keep its app allowlist narrow.
- GitHub downloads may need a proxy in mainland China. Lens uses the system proxy setting and configures none of its own.
- Pet packages are third-party content. Read one before you install it.
- To remove local data, exit Lens and delete `%APPDATA%\Lens`. This also removes your key and learning history. Delete the cloud data in Settings first, because it is not in that folder. Log out keeps the cloud copy.
- A forgotten sync passphrase cannot be recovered.

## Scope

This policy covers the Lens app and this repository. It makes no claims about Windows, your AI provider, custom endpoints, or other software on your PC. The project does not review, sign, or host pet packages.
