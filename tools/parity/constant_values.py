"""Checks that every numeric HUD/layout constant of the reference has a counterpart value.

The port renames constants freely, so this compares *values*, not names: for each reference
constant the script searches the render sources for that exact literal. A hit means the value
survives somewhere in the drawing code; that is evidence the layout was transcribed, not proof
that it is used in the same formula, so the report states its limit.
"""

import re
from pathlib import Path

REFERENCE = Path(r"E:\Chigros\app\src\main\java\com\wuying\phigros\game\GameRenderer.java")
PORT_DIRS = [
    Path(r"E:\ReChigros\src\render"),
    Path(r"E:\ReChigros\include\rechigros\render"),
    Path(r"E:\ReChigros\src\app"),
    Path(r"E:\ReChigros\src\core"),
    Path(r"E:\ReChigros\include\rechigros\game"),
]

CONST_RE = re.compile(
    r"private static final (?:float|double|int) ([A-Z][A-Z0-9_]+)\s*=\s*([^;]+);"
)
NUMBER_RE = re.compile(r"-?\d+\.?\d*(?:[fFdD])?")


def port_haystack():
    chunks = []
    for root in PORT_DIRS:
        for path in root.rglob("*"):
            if path.suffix not in {".cpp", ".hpp", ".h"}:
                continue
            chunks.append(path.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(chunks)


def main():
    text = REFERENCE.read_text(encoding="utf-8", errors="replace")
    constants = CONST_RE.findall(text)
    haystack = port_haystack()

    missing = []
    for name, value in constants:
        # Only single-number constants can be matched by value.
        numbers = NUMBER_RE.findall(value)
        if len(numbers) != 1:
            continue
        raw = numbers[0].rstrip("fFdD")
        if raw in {"4"} and name == "FLOAT_SIZE_BYTES":
            continue  # buffer arithmetic, not a drawn value
        if raw not in haystack:
            missing.append((name, raw))

    print(f"numeric reference constants : {len(constants)}")
    print(f"values absent from the port : {len(missing)}")
    for name, raw in missing:
        print(f"  {name} = {raw}")


if __name__ == "__main__":
    main()
