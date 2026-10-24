#!/usr/bin/env python3
"""List reference declarations that no parity report adjudicated, grouped by file.

The coverage matrix is a worklist; this prints the largest remaining groups so the next
pass can start where the risk is highest.
"""
import re
import sys
from collections import Counter

MATRIX = "docs/parity/coverage-matrix.md"


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

    print(f"declarations without a report verdict: {len(rows)}")
    by_file = Counter(path for path, _name, _status in rows)
    print("\nlargest groups:")
    for path, count in by_file.most_common(25):
        print(f"  {count:4d}  {path}")

    print("\nstatus split:")
    for status, count in Counter(status for _p, _n, status in rows).most_common():
        print(f"  {count:4d}  {status}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
