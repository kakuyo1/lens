# Security

Lens processes text from the Windows desktop and can send selected content to an AI provider chosen by the reader. This page describes the current data flow, safeguards, and known limits. It is not a guarantee that local data or third-party services are risk-free.

## Reporting a vulnerability

Please do not post exploitable details in a public issue. Check whether GitHub's private vulnerability reporting is available for this repository and use it when enabled. If it is unavailable, open a public issue asking for a private reporting channel, without including exploit details or sensitive data.

There is no published response-time or coordinated-disclosure SLA. Please include the affected version or commit, the impact, and steps to reproduce. Avoid sending real API keys, personal data, or private screen contents in a report. We will coordinate disclosure with the reporter when possible.

## Supported releases

The repository has no published release support window or response-time commitment. Do not assume that an older release will receive security fixes.

## Data and trust boundaries

- **Screen and selection data:** Selection capture, screenshot OCR, and optional automatic scanning can read text visible in other Windows applications. Automatic scanning can be limited to an application allowlist. Manual selection and screenshot capture are initiated by the reader.
- **OCR:** Screenshot regions are passed to the bundled or reader-configured Tesseract process on the same PC. OCR text is then filtered locally.
- **AI providers:** Lens sends the content needed for the selected request over HTTPS to the configured provider. Word requests contain the word, entity requests contain the entity name, and sentence requests contain the selected sentence. The provider's retention, training, and access practices are governed by that provider, not by Lens.
- **Local profile:** Settings, the API key, cached explanations, word marks, and learning history are stored in `%APPDATA%\Lens\settings.json`. This file is ordinary local JSON; Lens does not encrypt it with an application-managed key. Uninstalling Lens does not remove it. The GitHub token and the derived sync key are stored in the same file, so anyone who can read it can decrypt the cloud copy.
- **Cloud copy:** When sync is on, word marks, lookup history, statistics, and the selected settings are encrypted on this PC and stored as a secret gist in the reader's own GitHub account. GitHub can see that the gist exists, its size, its update times, and the IP addresses that access it, but not its contents. Explanation caches and the API key are never uploaded.
- **Third-party pet packages (specified, not yet implemented):** when the desktop pet is enabled, a reader can install a pet package under `%APPDATA%\Lens\pets\`. A package is data only: JSON documents and PNG images. Lens does not run code from a package, does not load a package's scripts, DLLs or QML, and does not fetch anything on a package's behalf. Packages are third-party content: Lens does not review, endorse or sign them and does not host or curate a registry for them. Lens reads that directory and never writes to it. The package contract, limits and validation rules are in `docs/pet-package-format.md`. This feature is specified but not implemented in released builds, which do not read that directory.
- **Logs:** The API key is not intentionally written to logs, but debug and trace logs can contain queried words and other operational details. Logs rotate and are written relative to the application's working directory, which may be inside the installation directory for packaged launches.
- **Clipboard:** Selection capture temporarily reads and changes the Windows clipboard, then attempts to restore its previous contents. Concurrent clipboard changes by another application can prevent a perfect restoration.

## Safeguards in the current code

The pet-package reader described under Data and trust boundaries is specified but not implemented. The list below covers what the code does today.

- Lens classifies and filters captured text locally, and sends only the content required for that request. It does not send a full-screen image to the explanation API.
- Before sending, Lens masks URLs, email addresses, and digit strings of six or more digits. This is limited pattern masking, not complete personal-data detection. Names, addresses, source code, short identifiers, and other sensitive content may remain.
- The API key is stored in the per-user settings file rather than the installation directory. Request and settings logs avoid printing the key itself.
- The sync passphrase is never stored. The key is derived with PBKDF2-HMAC-SHA256 at 600000 iterations and used with AES-256-GCM. Explanation caches are never uploaded.
- Model-list requests use HTTPS, a timeout, and response-size and item-count limits. Model responses are parsed and checked against the expected response shape before use.
- Sign-in and sync contact `github.com` and `api.github.com` over HTTPS through the system proxy setting, the same path the update check uses. Lens does not configure a proxy for them.
- Update checks make one HTTPS `GET` to this project's latest release page on GitHub and read the version from its redirect. The request carries only a `User-Agent: Lens/<version>` header: no API key, no selected or captured text, and no installation identifier. Redirects are not followed except to GitHub's release download path and its two asset hosts, at most five hops.
- The download path runs only when the reader presses Download on the update card. It fetches the release's `SHA256SUMS` and the installer for the newer version, writes the installer to `%LOCALAPPDATA%\Lens\updates` as a partial file, and renames it only after its SHA256 matches the published value. The digest is checked again immediately before the installer is started, which runs with no command-line arguments and is never silent.
- The checksum shows that the file arrived intact. It does not prove who published it: the installer is not code-signed, and a checksum taken from the same release cannot detect a compromised release. Run only releases you trust.
- Download sizes, stalls and connections are bounded. A checksum list is only read when a newer version is found, and a release that publishes no `SHA256SUMS` is not downloadable.
- OCR input, output, runtime, and child-process lifetime are bounded. OCR runs locally; the resulting text can still be sent to the selected provider if it passes local filtering and the reader's capture settings allow it.

## Reader guidance

- Do not send passwords, private messages, confidential work, personal records, or other material you are not comfortable sharing with your configured provider.
- Review that provider's privacy and retention terms before entering an API key. A custom HTTPS endpoint receives both the key and request content, so configure only an endpoint you trust.
- Leave automatic scanning off unless you need it. When enabled, keep its application allowlist narrow.
- Downloads come from GitHub and may need a proxy from mainland China. Lens does not configure a proxy for them; it uses the system proxy setting, and reports a download that never receives data with a proxy hint.
- Lens does not run a downloaded installer you have not pressed Install for. Delete `%LOCALAPPDATA%\Lens\updates` at any time; Lens recreates it when needed.
- A pet package is third-party content. Lens does not review or sign it. Read what you install, and prefer a package from a source you trust. A package cannot run code, but it can change the pet's appearance and the text shown for it, and anything you install stays in `%APPDATA%\Lens\pets\` until you delete it.
- Protect access to your Windows account and user profile. Anyone or any software running as your user may be able to read the settings file and logs.
- To remove local Lens data, exit the application and delete `%APPDATA%\Lens`. This also removes the saved API key, settings, cache, and learning history. It does not delete the cloud copy: use Delete cloud data in Settings first.
- A forgotten sync passphrase makes the cloud copy unrecoverable, and Lens cannot reset it.
- Log out removes the local token and derived key, and leaves the cloud copy in place. Delete cloud data deletes the gist and the local key, and keeps the token. Neither removes local learning data.

## Scope

This policy covers the Lens application and repository. It does not make claims about the security of Windows, the selected AI provider, custom endpoints, or other software running on the same PC. Third-party pet packages are content produced by others: Lens validates a package against `docs/pet-package-format.md` and does not execute its code, but it does not review, endorse, sign or host packages, and it makes no claim about how a package looks or behaves. Security support timelines and response SLAs have not been published.
