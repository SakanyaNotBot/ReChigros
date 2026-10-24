#!/usr/bin/env python3
"""Classify the reference declarations that no parity report adjudicated.

The coverage matrix lists 1,658 declarations without a per-declaration verdict. Most are not
gaps: the port restructures whole subsystems, so a reference method can be genuinely
implemented under a different name or absorbed into a different structure. This script sorts
the residue into categories that can each be checked as a group, so the remaining work is
bounded rather than an undifferentiated list.

Categories
  framework-hook     Android lifecycle / view callbacks with no analogue in an immediate-mode
                     port (onCreate, onPreDraw, onChildViewAttachedToWindow, ...)
  layout-builder     a helper whose whole job is constructing one widget subtree; the port
                     draws that subtree in a screen function instead
  accessor           trivial getters/setters/boilerplate
  named-helper       a curve/geometry helper the port expresses generically (verified by
                     `easing_compare.py` for the easing family)
  residual           anything else; these need individual attention
"""
import re
import sys
from collections import Counter

MATRIX = "docs/parity/coverage-matrix.md"

FRAMEWORK = {
    "oncreate", "onstart", "onresume", "onpause", "onstop", "ondestroy", "onrestart",
    "onsaveinstancestate", "onrestoreinstancestate", "onnewintent", "onbackpressed",
    "onwindowfocuschanged", "onconfigurationchanged", "onactivityresult", "onmeasure",
    "onlayout", "ondraw", "ontextchanged", "onsizechanged", "onpreviewtextchanged",
    "aftertextchanged", "beforetextchanged", "onpreDraw", "onpredraw",
    "onchildviewattachedtowindow", "onchildviewdetachedfromwindow",
    "onscrollchangelistener", "onscrollchange", "onsingleTapconfirmed", "ondoubletap",
    "ondown", "ontouchevent", "oncheckedchanged", "onprogresschanged", "onstarttrackingtouch",
    "onstoptrackingtouch", "onchange", "onvaluechange", "onfocuschange", "onclick",
    "callback", "onpreDrawlistener", "simplewatcher", "textwatcher", "oncreateview",
    "onviewcreated", "ondestroyview", "onattach", "ondetach",
}

ACCESSOR_PREFIX = ("get", "set", "is", "has")


def categorise(name: str) -> str:
    lower = name.lower()

    if lower in FRAMEWORK:
        return "framework-hook"

    if re.match(r"^(add|build|render|apply|update|setup|create|make|draw)[A-Z]", name):
        return "layout-builder"

    if re.match(r"^(get|set|is|has)[A-Z]", name) and len(name) < 40:
        return "accessor"

    # A private helper named after what it computes, e.g. computeXxxScale / xxxMix.
    if re.match(r"^(compute|elapsed|should|smooth)[A-Z]", name) or lower.endswith(
        ("mix", "progress", "alpha", "scale", "seconds", "factor")
    ):
        return "named-helper"

    return "residual"


def main() -> int:
    rows = []
    current = None
    with open(MATRIX, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("## `"):
                current = line.strip()[4:-1]
                continue
            match = re.match(r"\| (\d+) \| `([^`]+)` \| (unmatched|matched-by-name) \|", line)
            if match and current:
                rows.append((current, match.group(2), match.group(3)))

    by_category = Counter()
    residual_by_file = Counter()
    for path, name, _status in rows:
        category = categorise(name)
        by_category[category] += 1
        if category == "residual":
            residual_by_file[path] += 1

    print(f"unadjudicated declarations: {len(rows)}\n")
    print("by category:")
    for category, count in by_category.most_common():
        print(f"  {count:5d}  {category}")

    print("\nresidual (needs individual attention), by file:")
    for path, count in residual_by_file.most_common(20):
        print(f"  {count:5d}  {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
