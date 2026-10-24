#!/usr/bin/env python3
"""Print the unadjudicated declaration names for one reference file."""
import re
import sys
from collections import Counter

MATRIX = "docs/parity/coverage-matrix.md"


def main() -> int:
    needle = sys.argv[1] if len(sys.argv) > 1 else "GameRenderer"
    names = []
    current = None
    with open(MATRIX, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("## `"):
                current = line.strip()[4:-1]
                continue
            match = re.match(r"\| (\d+) \| `([^`]+)` \| (unmatched|matched-by-name) \|", line)
            if match and current and needle in current:
                names.append((match.group(3), match.group(2)))
    print(f"file~={needle}  total={len(names)}")
    for status, count in Counter(status for status, _ in names).most_common():
        print(f"  {count:4d}  {status}")
    print("\nfirst 60 names:")
    for index, (status, name) in enumerate(names[:60]):
        print(f"  {status:16s} {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
