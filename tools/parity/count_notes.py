#!/usr/bin/env python3
"""Independently parse a chart JSON and count its notes, as a cross-check on the port.

This is a second implementation of the reference's note-reading rules, written from the
reference source rather than from the port. Comparing its counts with the port's own
runtime totals is an independent check that the port reads the same notes out of the same
file: a parser that silently drops or duplicates notes shows up as a count difference even
when the picture looks plausible.

Supported here: the official format (`judgeLineList` with `notesAbove`/`notesBelow`), which
is what every shipped chart in the capture library uses.
"""
import json
import sys


def count_official(path):
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)

    lines = data.get("judgeLineList")
    if not isinstance(lines, list):
        return None

    total = 0
    fakes = 0
    holds = 0
    per_line = []
    for line in lines:
        if not isinstance(line, dict):
            continue
        line_total = 0
        for key in ("notesAbove", "notesBelow"):
            notes = line.get(key)
            if not isinstance(notes, list):
                continue
            for note in notes:
                if not isinstance(note, dict):
                    continue
                note_type = note.get("type")
                # The official format numbers notes 1..4; an out-of-range entry is dropped
                # by the reference rather than silently kept.
                try:
                    note_type = int(note_type)
                except (TypeError, ValueError):
                    note_type = 0
                if note_type < 1 or note_type > 4:
                    continue
                if note.get("isFake") is True:
                    fakes += 1
                    continue
                if note_type == 2:
                    holds += 1
                line_total += 1
        per_line.append(line_total)
        total += line_total

    return {
        "formatVersion": data.get("formatVersion"),
        "lines": len(lines),
        "notes": total,
        "fakes": fakes,
        "holds": holds,
        "perLine": per_line,
    }


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: count_notes.py <chart.json> [...]")
        return 2
    for path in sys.argv[1:]:
        result = count_official(path)
        if result is None:
            print(f"{path}: not an official-format chart")
            continue
        name = path.rsplit("\\", 1)[-1]
        print(f"{name}: formatVersion={result['formatVersion']} lines={result['lines']} "
              f"notes={result['notes']} holds={result['holds']} fakes={result['fakes']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
