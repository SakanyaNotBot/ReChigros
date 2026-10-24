#!/usr/bin/env python3
"""Compare the port's autoplay totals against an independent chart parse.

For each chart in the capture library this runs the port headlessly through a complete
autoplay performance and compares its reported totals with an independently computed note
count. A parser or judgement bug that drops, duplicates or mis-types notes shows up as a
mismatch between the two, which no screenshot comparison would reliably catch.

The run needs enough frames for the music to finish: the playhead advances in real time,
so the frame budget is derived from the music's own length rather than guessed.
"""
import json
import os
import re
import subprocess
import sys

APP = r"E:\ReChigros\build\desktop-release\src\app\ReChigros.exe"
DATA = r"E:\ReChigros\build\capture-data"
PACKS = os.path.join(DATA, "packs")
FPS = 60


def chart_files():
    for name in sorted(os.listdir(PACKS)):
        directory = os.path.join(PACKS, name)
        if not os.path.isdir(directory):
            continue
        for entry in sorted(os.listdir(directory)):
            if re.fullmatch(r"\d+\.json", entry):
                yield directory, os.path.join(directory, entry)


def count_notes(path):
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)
    lines = data.get("judgeLineList")
    if not isinstance(lines, list):
        return None
    total = 0
    for line in lines:
        if not isinstance(line, dict):
            continue
        for key in ("notesAbove", "notesBelow"):
            notes = line.get(key)
            if not isinstance(notes, list):
                continue
            for note in notes:
                if not isinstance(note, dict):
                    continue
                try:
                    note_type = int(note.get("type"))
                except (TypeError, ValueError):
                    continue
                if note_type < 1 or note_type > 4:
                    continue
                if note.get("isFake") is True:
                    continue
                total += 1
    return total


def chart_length_seconds(path):
    """Estimate the chart's length from its last note, for the frame budget.

    Note times are written either as a beat triple (RPE-style) or as a plain tick count on
    the 32-ticks-per-beat grid (official style); both are handled.
    """
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)
    lines = data.get("judgeLineList")
    if not isinstance(lines, list):
        return 180.0

    bpm = 120.0
    bpm_list = data.get("BPMList")
    if isinstance(bpm_list, list) and bpm_list:
        try:
            bpm = float(bpm_list[0].get("bpm", 120.0))
        except (AttributeError, TypeError, ValueError):
            bpm = 120.0
    if bpm <= 0:
        bpm = 120.0

    def beats_of(value):
        if isinstance(value, (int, float)):
            return float(value) / 32.0          # official: 32 ticks per beat
        if isinstance(value, list) and len(value) >= 3:
            try:
                whole, num, den = (float(part) for part in value[:3])
            except (TypeError, ValueError):
                return 0.0
            return whole + (num / den if den else 0.0)
        return 0.0

    last_beat = 0.0
    for line in lines:
        if not isinstance(line, dict):
            continue
        line_bpm = bpm
        try:
            candidate = float(line.get("bpm", bpm))
            if candidate > 0:
                line_bpm = candidate
        except (TypeError, ValueError):
            pass
        for key in ("notesAbove", "notesBelow"):
            notes = line.get(key)
            if not isinstance(notes, list):
                continue
            for note in notes:
                if not isinstance(note, dict):
                    continue
                if note.get("isFake") is True:
                    continue
                last_beat = max(last_beat, beats_of(note.get("time")))
    seconds = last_beat * 60.0 / bpm
    return max(30.0, seconds + 25.0)


def main() -> int:
    only = sys.argv[1] if len(sys.argv) > 1 else ""
    failures = 0
    checked = 0
    for directory, chart in chart_files():
        if only and only not in chart:
            continue
        expected = count_notes(chart)
        if expected is None:
            print(f"skip {os.path.basename(chart)}: not an official-format chart")
            continue
        seconds = chart_length_seconds(chart)
        # The music plays in real time, so the frame budget follows its length with margin.
        frames = int(seconds * FPS) + FPS * 30
        result = subprocess.run(
            [APP, "--data-root", DATA, chart, "--frames", str(frames),
             "--fixed-delta", "--autoplay", "--expect-result"],
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=3600,
        )
        output = (result.stdout or "") + (result.stderr or "")
        match = re.search(
            r"result: score=\d+ accuracy=[\d.]+ maxCombo=(\d+) perfect=(\d+) good=(\d+) "
            r"bad=(\d+) miss=(\d+) total=(\d+)", output)
        if not match:
            tail = output.strip().splitlines()[-3:]
            print(f"NO-RESULT {os.path.basename(chart)} (exit {result.returncode}, "
                  f"frames={frames})  {tail}")
            failures += 1
            continue
        max_combo, perfect, good, bad, miss, total = (int(value) for value in match.groups())
        checked += 1
        judgements = perfect + good + bad + miss
        ok = (total == expected) and (judgements == expected) and (max_combo == expected)
        print(f"{os.path.basename(chart)}: parse={expected} total={total} judged={judgements} "
              f"combo={max_combo} -> {'OK' if ok else 'MISMATCH'}")
        if not ok:
            failures += 1
    print(f"\n{checked} chart(s) verified, {failures} problem(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
