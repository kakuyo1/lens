# Wordlist Source

`wordlist.txt` is a normalized, one-word-per-line coverage list used by the local selection
classifier. It is not a dictionary and carries no definitions.

It is built by `scripts/data/build-wordlist.py` from two sources:

- **Open English WordNet 2022** — the single lowercase ASCII words of its noun, verb, adjective
  and adverb indexes: 78,613. Source: <https://en-word.net/> (the NLTK `wordnet2022` corpus
  package is one mirror). It is derived from Princeton WordNet under the WordNet License and
  further developed by the Open English WordNet team under CC BY 4.0; redistribution must credit
  both. The full licence text ships with the corpus (`wordnet2022/LICENSE`).
- **ECDICT** — the 49,139 lowercase ASCII entries with a Collins or Oxford mark, an exam tag or a
  corpus rank, plus the `exchange` forms of the coverage words: 63,575. Source:
  <https://github.com/skywind3000/ECDICT>. MIT licensed; the notice with the full text is in
  `data/wordbooks/NOTICE.md`.

The file holds 152,058 words: 48,335 ranked base words, 40,148 unranked base words, and 63,575
exchange forms. Only `^[a-z]+$` entries are kept; capitalized entries (proper nouns and
acronym-style names), multi-word entries, and entries with digits, apostrophes, hyphens or
non-ASCII letters are excluded.

Line position is the rank the application reads (`loadWordlist`). Ranked base words come first in
rank order, where rank is the smaller positive of ECDICT's `frq` (contemporary corpus) and `bnc`
(BNC) columns; unranked base words follow alphabetically; the exchange forms sit in the tail, and
their base's rank masters them through the application's form lookup. The tail has no frequency
meaning.

`scan-words.txt` is the pool the automatic scan draws from (docs/adr/0186): the 42,870 ranked
base words with an English definition or a Collins/Oxford/exam mark, plus their 37,864 forms.
A word outside the pool is still explained when the reader selects it; it is simply not drawn on
the page by the scan.

`wordlist-extra.txt` is the hand-kept supplement for words both sources miss (spoken forms and
modern jargon). It is unranked, never drawn by the scan, and exists only so a selection is
explained as a word rather than an entity.

## Rebuilding

```
py -3 -I scripts/data/build-wordlist.py ^
    --wordnet <wordnet2022-dir> ^
    --ecdict <stardict.csv|stardict.db> ^
    --output data/wordlist.txt ^
    --scan-pool data/scan-words.txt ^
    --extra data/wordlist-extra.txt
```

The inputs are not in the repository. The 2026-11-16 build used the `stardict.csv` extracted from
ECDICT's `stardict.7z` (232,668,349 bytes, 2025-01-02) and the NLTK `wordnet2022` corpus. The
script is deterministic: the same inputs produce the same bytes.
