"""Re-flows a C++ source file that lost its newlines.

Splits after statement terminators and brace boundaries while tracking string, character and
comment state so nothing inside a literal or a comment is broken. Deterministic: the same input
always yields the same output, and the token stream is unchanged.
"""

import sys
from pathlib import Path

QUOTE = '"'
APOS = "'"
SLASH = "/"


def reline(text: str) -> str:
    out = []
    state = None
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        out.append(ch)
        if state is None:
            if ch == QUOTE:
                state = "str"
            elif ch == APOS:
                state = "chr"
            elif ch == SLASH and i + 1 < n and text[i + 1] == SLASH:
                state = "line"
                out.append(SLASH)
                i += 1
            elif ch == SLASH and i + 1 < n and text[i + 1] == "*":
                state = "block"
                out.append("*")
                i += 1
            elif ch in ";{}":
                out.append("\n")
        elif state == "str":
            if ch == "\\":
                out.append(text[i + 1])
                i += 1
            elif ch == QUOTE:
                state = None
        elif state == "chr":
            if ch == "\\":
                out.append(text[i + 1])
                i += 1
            elif ch == APOS:
                state = None
        elif state == "line":
            if ch == "\n":
                state = None
        elif state == "block":
            if ch == "*" and i + 1 < n and text[i + 1] == SLASH:
                out.append(SLASH)
                i += 1
                state = None
        i += 1
    return "".join(out)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: reline.py <file>", file=sys.stderr)
        return 2
    path = Path(sys.argv[1])
    original = path.read_text(encoding="utf-8")
    reflowed = reline(original)
    path.write_text(reflowed, encoding="utf-8", newline="\n")
    print(f"relined {path}: {len(reflowed)} chars, {reflowed.count(chr(10))} lines")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
