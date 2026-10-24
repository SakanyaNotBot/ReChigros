"""Cross-references the reference renderer's methods against the port.

A missing name is not proof of a missing behaviour: the port restructures classes and renames
freely. It is a worklist. To keep the report honest the script reports three buckets:

  * present    - a symbol with the same name exists somewhere in the port sources
  * alias      - no same-name symbol, but a snake/lower-cased form of the name exists
  * unmatched  - neither; needs a manual verdict
"""

import re
import sys
from pathlib import Path

REFERENCE = Path(r"E:\Chigros\app\src\main\java\com\wuying\phigros\game\GameRenderer.java")
PORT_ROOTS = [Path(r"E:\ReChigros\src"), Path(r"E:\ReChigros\include")]

METHOD_RE = re.compile(
    r"\b(?:void|float|double|boolean|int|long|String|Texture|HoldUv|RectF|List<[^>]+>)"
    r"\s+([a-z][a-zA-Z0-9_]{3,})\s*\("
)


def reference_methods():
    text = REFERENCE.read_text(encoding="utf-8", errors="replace")
    return sorted(set(METHOD_RE.findall(text)))


def port_text():
    chunks = []
    for root in PORT_ROOTS:
        for path in root.rglob("*"):
            if path.suffix not in {".cpp", ".hpp", ".h", ".cc"}:
                continue
            try:
                chunks.append(path.read_text(encoding="utf-8", errors="replace"))
            except OSError:
                pass
    return "\n".join(chunks)


def to_snake(name):
    out = []
    for index, ch in enumerate(name):
        if ch.isupper():
            if index and (not name[index - 1].isupper()):
                out.append("_")
            out.append(ch.lower())
        else:
            out.append(ch)
    return "".join(out)


def main():
    methods = reference_methods()
    haystack = port_text()
    present, alias, unmatched = [], [], []
    for name in methods:
        if name in haystack:
            present.append(name)
            continue
        snake = to_snake(name)
        # Match the snake form only at a word boundary, to avoid substring hits.
        if re.search(r"\b%s\b" % re.escape(snake), haystack):
            alias.append(name)
            continue
        unmatched.append(name)

    print(f"reference methods : {len(methods)}")
    print(f"present by name   : {len(present)}")
    print(f"present as alias  : {len(alias)}")
    print(f"unmatched         : {len(unmatched)}")
    out = Path(r"E:\ReChigros\build\unmatched_methods.txt")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(unmatched), encoding="utf-8")
    print(f"worklist written  : {out}")
    if len(sys.argv) > 1 and sys.argv[1] == "--show":
        for name in unmatched[:60]:
            print("  ", name)


if __name__ == "__main__":
    main()
