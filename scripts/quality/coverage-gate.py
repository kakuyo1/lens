#!/usr/bin/env python3
"""The coverage gate: thresholds on the merged Cobertura report, plus the lines the change added.

Two thresholds, both from the goal the gate serves:

- **overall** (75): the unioned line coverage of everything the report names must reach it.
- **changed** (85): the lines the change ADDED, inside files the report names, must reach it.

The union is the same walk coverage-summary.py does: OpenCppCoverage's report can carry one
<class> per binary a source file was linked into, and the root line-rate sums those copies, so
reading it directly reads low. Per file, the line numbers are unioned before either rate.

The changed-lines denominator only counts lines the report knows: a file no scan target links
(main.cpp, the reporter) is not measured by any scan, and the metric doc's own rule is that
"not reported" is not 0%. An added comment or blank line is not an executable line either, and
drops out the same way -- the gate judges what coverage can see, which is the same set the
per-directory numbers in docs/metrics/ judge.

Usage:
  python scripts/quality/coverage-gate.py <coverage.xml> [--base <sha>]
      [--overall 75] [--changed 85]

--base is the commit the change is measured against; without it the gate falls back to HEAD^,
which on a merge commit is the pre-merge state and on a push is the change itself. With no
repository at all (a bare report handed to the script) the changed gate is skipped and only the
overall threshold runs.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict

# The repository's own sources, below the drive letter the report names them by. The same mark
# coverage-summary.py uses; the leading separator keeps a directory named "src-old" from matching.
MARK = "\\src\\"


def unioned_lines(report: str) -> tuple[dict[str, set[int]], dict[str, set[int]]]:
    """Union the per-binary copies of every source file into covered/valid line sets."""
    covered: dict[str, set[int]] = defaultdict(set)
    valid: dict[str, set[int]] = defaultdict(set)
    for cls in ET.parse(report).iter("class"):
        filename = cls.get("filename", "")
        if MARK not in filename:
            continue
        relative = filename.split(MARK, 1)[1].replace("\\", "/")
        for line in cls.iter("line"):
            number = int(line.get("number"))
            valid[relative].add(number)
            if int(line.get("hits")) > 0:
                covered[relative].add(number)
    return covered, valid


def added_lines(base: str, head: str) -> dict[str, set[int]]:
    """The lines the change added under src/, keyed the way the report names its files.

    The Cobertura report keys a file by its path under src/ -- "app/duties/card_session.cpp",
    no "src/" prefix -- so the diff's keys are brought to the same shape here; files outside
    src/ are not measured by any scan and drop out at the key stage.
    """
    diff = subprocess.run(
        ["git", "diff", "-U0", "--no-color", base, head, "--", "src/"],
        capture_output=True, text=True, check=True,
    ).stdout
    added: dict[str, set[int]] = defaultdict(set)
    file = None
    newline = 0
    for line in diff.splitlines():
        if line.startswith("+++ b/"):
            file = line[6:].split("\t")[0]
            if file.startswith("src/"):
                file = file[4:]
            else:
                file = None
        elif line.startswith("@@") and file:
            # The hunk header's second span is the new file's range; -U0 makes it one line long.
            after = line.split("+", 1)[1].split(" ", 1)[0]
            newline = int(after.split(",")[0])
        elif file and line.startswith("+") and not line.startswith("+++"):
            added[file].add(newline)
            newline += 1
    return added


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("report", help="the merged Cobertura XML")
    parser.add_argument("--base", help="the commit the change is measured against (default HEAD^)")
    parser.add_argument("--overall", type=float, default=75.0)
    parser.add_argument("--changed", type=float, default=85.0)
    args = parser.parse_args()

    covered, valid = unioned_lines(args.report)
    hit = sum(len(covered[f]) for f in valid)
    total = sum(len(valid[f]) for f in valid)
    overall_rate = hit / total * 100 if total else 0.0
    print(f"coverage-gate: overall {hit}/{total} = {overall_rate:.1f}% (threshold {args.overall:g}%)")

    failures = []
    if overall_rate < args.overall:
        failures.append(f"overall coverage {overall_rate:.1f}% is below the {args.overall:g}% baseline")

    base = args.base
    if not base:
        base = subprocess.run(["git", "rev-parse", "--verify", "HEAD^"], capture_output=True, text=True).stdout.strip()
    changed = None
    if base:
        try:
            added = added_lines(base, "HEAD")
        except subprocess.CalledProcessError as error:
            failures.append(f"the change could not be diffed against {base}: {error.stderr.strip()}")
            added = None
        if added:
            measured: dict[str, set[int]] = {}
            for file, lines in sorted(added.items()):
                if file in valid:
                    executable = lines & valid[file]
                    if executable:
                        measured[file] = executable
                # A file the report does not name is not measured by any scan target; the metric
                # doc's rule is that "not reported" is not 0%, so it stays out of the denominator.
            skipped = len(added) - len(measured)
            changed_hit = sum(len(covered[f] & lines) for f, lines in measured.items())
            changed_total = sum(len(lines) for lines in measured.values())
            changed_rate = changed_hit / changed_total * 100 if changed_total else None
            if changed_rate is None:
                print("coverage-gate: no added executable lines under src/ in this change")
            else:
                print(f"coverage-gate: changed lines {changed_hit}/{changed_total} = {changed_rate:.1f}% "
                      f"(threshold {args.changed:g}%, {skipped} added file(s) not in any scan target)")
                if changed_rate < args.changed:
                    failures.append(f"added lines are {changed_rate:.1f}% covered, below the {args.changed:g}% gate")
                    for file, lines in sorted(measured.items()):
                        missed = sorted(lines - covered[file])
                        if missed:
                            listing = ", ".join(str(n) for n in missed[:20])
                            more = "" if len(missed) <= 20 else f" (+{len(missed) - 20} more)"
                            print(f"coverage-gate:   {file}: uncovered added lines {listing}{more}")
            changed = changed_rate

    if failures:
        for failure in failures:
            print(f"coverage-gate: FAIL {failure}")
        raise SystemExit(1)
    tail = f"; changed lines {changed:.1f}%" if changed is not None else ""
    print(f"coverage-gate: PASS (overall {overall_rate:.1f}%{tail})")


if __name__ == "__main__":
    main()
