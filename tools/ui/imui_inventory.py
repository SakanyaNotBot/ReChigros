#!/usr/bin/env python3
"""Counts every interface drawing entry point so the migration can be tracked.

The interface is drawn through one of a small number of entry points: the full-screen
container that each screen opens, the shared drawing helpers, and direct calls into the
immediate-mode interface library. This script reports them per file, so "how much is left
to move" is a measurement rather than an impression.

Run from the repository root:

    python tools/ui/imui_inventory.py
    python tools/ui/imui_inventory.py --json build/ui-inventory.json
"""

import argparse
import collections
import json
import pathlib
import sys

ENTRY_POINTS = (
    "beginFullscreenWindow",
    "endFullscreenWindow",
    "ui::drawLabel(",
    "ui::drawPanel(",
    "ui::drawPanelLine(",
    "ui::skewButton(",
    "ui::iconButton(",
    "ui::drawText",
    "ui::drawTextCentered",
    "ui::measureText",
    "ui::drawImage",
    "ui::drawBackdrop",
    "ImGui::",
)

# Files that are the layer under migration rather than a screen using it.
FOUNDATION = (
    "src/app/UiStyle.cpp",
    "src/app/UiLayout.cpp",
    "src/app/UiText.cpp",
    "include/rechigros/app/UiStyle.hpp",
    "include/rechigros/app/UiWidgets.hpp",
    "include/rechigros/app/UiLayout.hpp",
)

# Not a screen: verification and the application shell.
INFRASTRUCTURE = (
    "src/app/Application.cpp",
    "src/app/SelfTest.cpp",
    "src/app/ScreenCommon.cpp",
    "src/app/main.cpp",
)


def classify(path: str) -> str:
    if path in FOUNDATION:
        return "foundation"
    if path in INFRASTRUCTURE:
        return "infrastructure"
    return "screen"


def scan(root: pathlib.Path):
    files = sorted(root.glob("src/**/*.cpp")) + sorted(root.glob("include/**/*.hpp"))
    files = [f for f in files if "third_party" not in str(f)]
    rows = []
    totals = collections.Counter()
    for path in files:
        text = path.read_text(encoding="utf-8", errors="replace")
        counts = collections.Counter()
        for entry in ENTRY_POINTS:
            found = text.count(entry)
            if found:
                counts[entry.rstrip("(")] = found
        if not counts:
            continue
        relative = path.relative_to(root).as_posix()
        rows.append(
            {
                "file": relative,
                "class": classify(relative),
                "counts": dict(counts),
                "total": sum(counts.values()),
            }
        )
        totals.update(counts)
    rows.sort(key=lambda row: (-row["total"], row["file"]))
    return rows, totals


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".", help="repository root to scan")
    parser.add_argument("--json", help="also write the raw counts here")
    arguments = parser.parse_args()

    root = pathlib.Path(arguments.root).resolve()
    rows, totals = scan(root)

    print(f"{'file':<46} {'class':<14} total")
    for row in rows:
        print(f"{row['file']:<46} {row['class']:<14} {row['total']}")
    print()
    by_class = collections.Counter()
    for row in rows:
        by_class[row["class"]] += row["total"]
    print("by class:", dict(by_class))
    print("by entry point:", dict(totals))
    print("grand total:", sum(totals.values()))

    if arguments.json:
        target = root / arguments.json
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(
            json.dumps({"rows": rows, "totals": dict(totals), "byClass": dict(by_class)},
                       indent=2, ensure_ascii=False),
            encoding="utf-8",
        )
        print("wrote", target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
