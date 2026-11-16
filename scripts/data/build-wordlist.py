"""Build data/wordlist.txt and data/scan-words.txt from Open English WordNet and ECDICT.

Usage (run with -I so nothing next to the inputs is imported):
    py -3 -I scripts/data/build-wordlist.py --wordnet <wordnet-dir> --ecdict <stardict.csv|.db> \
        --output <wordlist.txt> --scan-pool <scan-words.txt> [--extra <extra.txt>]

`--wordnet` is a directory holding the WN-LMF/WNDB index files index.noun, index.verb,
index.adj and index.adv (the NLTK `wordnet2022` corpus works as-is). `--ecdict` is either
the full `stardict.csv` or a sqlite database with the same `stardict` table the wordbook
builder uses. Both sources and their licences are recorded in data/wordlist-source.md.

The output is one lowercase ASCII word per line:

- the coverage list holds every single-word lemma of Open English WordNet 2022, every ECDICT
  entry with a Collins/Oxford mark, an exam tag or a corpus rank, and the ECDICT `exchange`
  forms of those words (so inflections the strict lemmatiser keeps as-is are still covered);
- the scan pool holds only the words the corpus ranks and the dictionary annotates: a rank
  with no English definition and no exam mark is a name or an abbreviation, not a word to
  teach. Automatic scanning draws from this pool; an explicit selection is explained either way;
- line position is the rank the application reads: base words come first in rank order, then the
  unranked base words alphabetically, then the exchange forms. A form inherits its base's rank
  through the application's form lookup, so the form tail carrying no frequency meaning is fine.

`--extra` names a hand-kept supplement (spoken forms and modern jargon the sources miss) and
is appended unranked. The script is deterministic: same inputs, same bytes.
"""
import argparse
import csv
import io
import re
import sqlite3
import sys
from pathlib import Path

LETTERS = re.compile(r"[a-z]+")
# exchange: d past participle, p past, i -ing, 3 third person, r comparative, t superlative,
# s plural, 0 lemma. The other slots hold type codes, not words.
FORM_TYPES = {"d", "p", "i", "3", "r", "t", "s", "0"}


def wordnet_words(directory: Path) -> set[str]:
    """@return The single lowercase ASCII words Open English WordNet spells as lemmas."""
    words: set[str] = set()
    for name in ("index.noun", "index.verb", "index.adj", "index.adv"):
        path = directory / name
        if not path.exists():
            sys.exit(f"missing WordNet index: {path}")
        with io.open(path, encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if line.startswith("  "):
                    continue  # the licence header sits in the first lines
                lemma = line.split(" ", 1)[0]
                if LETTERS.fullmatch(lemma):
                    words.add(lemma)
    return words


def ecdict_rows(path: Path):
    """Yield (word, bnc, frq, collins, oxford, tag, definition, exchange) as ECDICT stores them."""
    if path.suffix.lower() in (".db", ".sqlite", ".sqlite3"):
        connection = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
        try:
            query = "SELECT word, bnc, frq, collins, oxford, tag, definition, exchange FROM stardict"
            for row in connection.execute(query):
                yield tuple(value if value is not None else "" for value in row)
        finally:
            connection.close()
        return
    with io.open(path, encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            yield (row["word"], row["bnc"], row["frq"], row["collins"], row["oxford"],
                   row["tag"], row["definition"], row["exchange"])


def as_int(value) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return 0


def exchange_forms(exchange) -> list[str]:
    forms: list[str] = []
    for part in str(exchange or "").split("/"):
        label, _, value = part.partition(":")
        if label in FORM_TYPES and LETTERS.fullmatch(value):
            forms.append(value)
    return forms


def build(wordnet: set[str], rows, extra: list[str]):
    """@return (wordlist, scan_pool, stats) as ordered lists of words plus a counter dict."""
    records: dict[str, dict] = {}
    for word, bnc, frq, collins, oxford, tag, definition, exchange in rows:
        if not LETTERS.fullmatch(str(word or "")):
            continue
        bnc, frq = as_int(bnc), as_int(frq)
        annotated = bool(as_int(collins) or as_int(oxford) or (tag or "").strip())
        has_definition = bool((definition or "").strip())
        rank = min([value for value in (frq, bnc) if value > 0], default=0)
        previous = records.get(word)
        if previous is None:
            records[word] = {
                "rank": rank,
                "annotated": annotated,
                "hasdef": has_definition,
                "forms": exchange_forms(exchange),
            }
            continue
        # Duplicates keep the earliest usable rank and merge the annotations.
        if previous["rank"] == 0 or (rank and rank < previous["rank"]):
            previous["rank"] = rank
        previous["annotated"] = previous["annotated"] or annotated
        previous["hasdef"] = previous["hasdef"] or has_definition
        previous["forms"].extend(exchange_forms(exchange))

    def covered(record: dict) -> bool:
        return record["rank"] > 0 or record["annotated"]

    def markable(record: dict) -> bool:
        # A rank with neither an English definition nor an exam mark is a name or an
        # abbreviation the corpus happened to contain, not a word to teach.
        return record["rank"] > 0 and (record["annotated"] or record["hasdef"])

    coverage: dict[str, int] = {}
    forms: dict[str, int] = {}
    pool: dict[str, int] = {}
    pool_forms: dict[str, int] = {}
    stats = {"wordnet": 0, "ecdict": 0, "forms": 0, "extra": 0}

    def add_base(word: str, rank: int, teach: bool, source: str) -> None:
        if word in coverage or word in forms:
            return
        coverage[word] = rank
        stats[source] += 1
        if teach:
            pool[word] = rank

    def add_form(form: str, rank: int, teach: bool) -> None:
        if form in coverage or form in forms:
            return
        forms[form] = rank
        stats["forms"] += 1
        if teach:
            pool_forms[form] = rank

    for word in sorted(wordnet):
        record = records.get(word)
        if record is None:
            add_base(word, 0, False, "wordnet")
        else:
            add_base(word, record["rank"], markable(record), "wordnet")

    for word in sorted(records):
        record = records[word]
        if covered(record):
            add_base(word, record["rank"], markable(record), "ecdict")

    for word in extra:
        if LETTERS.fullmatch(word):
            add_base(word, 0, False, "extra")

    for word in sorted(wordnet):
        record = records.get(word)
        if record is not None:
            for form in record["forms"]:
                add_form(form, record["rank"], markable(record))

    for word in sorted(records):
        record = records[word]
        if covered(record):
            for form in record["forms"]:
                add_form(form, record["rank"], markable(record))

    base_order = lambda item: (item[1] == 0, item[1], item[0])
    wordlist = [word for word, _ in sorted(coverage.items(), key=base_order)]
    wordlist += sorted(forms)
    scan_pool = [word for word, _ in sorted(pool.items(), key=base_order)]
    scan_pool += sorted(pool_forms)
    stats["ranked_bases"] = sum(1 for rank in coverage.values() if rank > 0)
    stats["unranked_bases"] = len(coverage) - stats["ranked_bases"]
    stats["pool_bases"] = len(pool)
    stats["pool_forms"] = len(pool_forms)
    return wordlist, scan_pool, stats


def write(path: Path, words: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with io.open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("".join(f"{word}\n" for word in words))
    print(f"{path}: {len(words)} words")


def main(argv: list[str]) -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--wordnet", required=True, type=Path)
    parser.add_argument("--ecdict", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--scan-pool", required=True, type=Path)
    parser.add_argument("--extra", type=Path)
    args = parser.parse_args(argv)

    extra: list[str] = []
    if args.extra and args.extra.exists():
        with io.open(args.extra, encoding="utf-8") as handle:
            extra = [line.strip().lower() for line in handle
                     if line.strip() and not line.startswith("#")]
    elif args.extra:
        sys.exit(f"missing extra list: {args.extra}")

    wordlist, scan_pool, stats = build(wordnet_words(args.wordnet), ecdict_rows(args.ecdict), extra)
    write(args.output, wordlist)
    write(args.scan_pool, scan_pool)
    print(f"coverage: {stats['ranked_bases']} ranked base words, {stats['unranked_bases']} unranked; "
          f"scan pool: {stats['pool_bases']} bases + {stats['pool_forms']} forms; sources: "
          f"WordNet-only {stats['wordnet']}, ECDICT {stats['ecdict']}, "
          f"exchange forms {stats['forms']}, extra {stats['extra']}")


if __name__ == "__main__":
    main(sys.argv[1:])
