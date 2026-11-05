"""Build the level wordbooks in data/wordbooks/ from the public sources named in data/wordbooks/NOTICE.md.

Usage (run with -I so nothing next to the inputs is imported):
    py -3 -I scripts/data/build-wordbooks.py <ecdict-stardict.db> <cefrj.csv> <octanove.csv>

Each book is one lowercase ASCII lemma per line, sorted, kept only when data/wordlist.txt holds it.
The CEFR books are cumulative: B1-B2 includes A1 and A2, C1-C2 includes everything below it.
"""
import csv
import re
import sqlite3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "data" / "wordbooks"
LEMMA = re.compile(r"[a-z]+")


def static_words():
    text = (ROOT / "data" / "wordlist.txt").read_text(encoding="utf-8")
    return {line.strip() for line in text.splitlines() if line.strip()}


def ecdict_tag(db, tag, static):
    con = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
    try:
        rows = con.execute("SELECT word FROM stardict WHERE ' ' || tag || ' ' LIKE ?", (f"% {tag} %",)).fetchall()
    finally:
        con.close()
    return {word for (word,) in rows if LEMMA.fullmatch(word) and word in static}


def cefr_levels(path, levels, static):
    with open(path, encoding="utf-8-sig", newline="") as f:
        return {
            row["headword"]
            for row in csv.DictReader(f)
            if row["CEFR"] in levels and LEMMA.fullmatch(row["headword"]) and row["headword"] in static
        }


def write(name, words):
    (OUT / name).write_text("".join(f"{word}\n" for word in sorted(words)), encoding="utf-8", newline="\n")
    print(f"{name}: {len(words)}")


def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__)
    db, cefrj, octanove = argv
    static = static_words()
    OUT.mkdir(parents=True, exist_ok=True)
    for tag in ("cet4", "cet6", "toefl", "ielts"):
        write(f"{tag}.txt", ecdict_tag(db, tag, static))
    basic = cefr_levels(cefrj, {"A1", "A2", "B1", "B2"}, static)
    advanced = cefr_levels(octanove, {"C1", "C2"}, static)
    write("b1-b2.txt", basic)
    write("c1-c2.txt", basic | advanced)


if __name__ == "__main__":
    main(sys.argv[1:])
