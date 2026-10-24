#!/usr/bin/env python3
"""Print residual (unclassified) unadjudicated declaration names for one reference file."""
import re
import sys

from classify_residue import categorise

MATRIX = "docs/parity/coverage-matrix.md"
HEADING = "## " + chr(96)


def main() -> int:
    needle = sys.argv[1] if len(sys.argv) > 1 else "GameRenderer"
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 80
    current = None
    names = []
    with open(MATRIX, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith(HEADING):
                current = line.strip()[3:].strip().strip(chr(96))
                continue
            match = re.match(r"\| (\d+) \| ([^|]+?) \| (unmatched|matched-by-name) \|", line)
            if match and current and needle in current:
                name = match.group(2).strip().strip(chr(96)).strip()
                if categorise(name) == "residual":
                    names.append((match.group(1), name))
    print(f"{needle}: {len(names)} residual")
    for line_number, name in names[:limit]:
        print(f"  {line_number:>6}  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
