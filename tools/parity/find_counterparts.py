#!/usr/bin/env python3
"""Report reference declarations whose behaviour is implemented under a different name.

A name-only match under-reports coverage badly for this port: whole subsystems were
restructured, so a reference helper can be implemented with a different name and the same
behaviour. This script looks for the *evidence* that a counterpart exists, independently of
the name:

  * a distinctive numeric constant the declaration's body uses appears in the port tree
  * a distinctive string literal the declaration produces appears in the port tree
  * a distinctive identifier from the declaration's body appears in the port tree

It is a heuristic and says so: a hit means "a human should compare these two", not "verified".
The output is grouped by confidence so the remaining work is ordered.
"""
import json
import os
import re
import sys
from collections import Counter

JAVA_ROOT = r"E:\Chigros\app\src\main\java"
CPP_ROOTS = [r"E:\ReChigros\src", r"E:\ReChigros\include", r"E:\ReChigros\platform"]
SKIP = {"build", ".git", "third_party", ".gradle", ".idea", "artifacts", ".cxx",
        "node_modules", "test-scratch", "perf-final", "docs"}

NUM = re.compile(r"(?<![\w.])(\d+\.\d{3,})(?![\w.])")
STR = re.compile(r'"([^"\\]{6,})"')
IDENT = re.compile(r"\b([a-z][A-Za-z0-9]{7,})\b")


def read(path):
    for encoding in ("utf-8", "gbk", "latin-1"):
        try:
            with open(path, encoding=encoding) as handle:
                return handle.read()
        except UnicodeDecodeError:
            continue
    return ""


def iter_files(roots, exts):
    for root in roots:
        for base, dirs, files in os.walk(root):
            dirs[:] = [d for d in dirs if d not in SKIP and not d.startswith(".")]
            for name in files:
                if os.path.splitext(name)[1] in exts:
                    yield os.path.join(base, name)


def method_bodies(text):
    """Yield (name, start_line, body_text) for each brace-delimited method body."""
    lines = text.splitlines()
    index = 0
    count = len(lines)
    while index < count:
        line = lines[index]
        match = re.match(r"\s*(?:private|public|protected|static|final|\s)*[\w<>\[\], .?]+\s+"
                         r"(\w+)\s*\([^;]*$", line)
        if match and "{" not in line:
            # Signature may wrap; gather up to the opening brace.
            header = [line]
            cursor = index
            while cursor + 1 < count and "{" not in lines[cursor] and cursor - index < 6:
                cursor += 1
                header.append(lines[cursor])
            if "{" in lines[cursor]:
                depth = 0
                body = []
                scan = cursor
                started = False
                while scan < count:
                    current = lines[scan]
                    depth += current.count("{") - current.count("}")
                    if "{" in current:
                        started = True
                    if started:
                        body.append(current)
                    if started and depth <= 0:
                        break
                    scan += 1
                yield match.group(1), index + 1, "\n".join(body)
                index = scan + 1
                continue
        index += 1


def build_port_corpus():
    numbers = set()
    strings = set()
    identifiers = set()
    for path in iter_files(CPP_ROOTS, {".cpp", ".hpp", ".h", ".glsl"}):
        text = read(path)
        numbers.update(NUM.findall(text))
        strings.update(STR.findall(text))
        identifiers.update(IDENT.findall(text))
    return numbers, strings, identifiers


def main() -> int:
    target = sys.argv[1] if len(sys.argv) > 1 else ""
    matrix = "docs/parity/coverage-matrix.md"
    if not os.path.isfile(matrix):
        print(f"missing {matrix}")
        return 2

    # Reference declarations that no report adjudicated.
    pending = {}
    current = None
    heading = "#" + "# " + chr(96)
    with open(matrix, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith(heading):
                current = line.strip()[3:].strip().strip(chr(96))
                continue
            match = re.match(r"\| (\d+) \| ([^|]+?) \| (unmatched|matched-by-name) \|", line)
            if match and current:
                name = match.group(2).strip().strip(chr(96)).strip()
                pending.setdefault(current, []).append((int(match.group(1)), name))

    numbers, strings, identifiers = build_port_corpus()
    print(f"port corpus: {len(numbers)} decimal literals, {len(strings)} strings, "
          f"{len(identifiers)} identifiers")

    report = []
    for rel_path, items in pending.items():
        if target and target not in rel_path:
            continue
        java_path = os.path.join(JAVA_ROOT, rel_path)
        if not os.path.isfile(java_path):
            continue
        text = read(java_path)
        wanted = {name: line for line, name in items}
        for name, line_number, body in method_bodies(text):
            if name not in wanted:
                continue
            hits = []
            for value in NUM.findall(body):
                if value in numbers:
                    hits.append(f"num {value}")
            for value in STR.findall(body):
                if value in strings:
                    hits.append(f"str {value[:40]}")
            for value in IDENT.findall(body):
                if value in identifiers:
                    hits.append(f"id {value}")
            if hits:
                report.append((len(hits), rel_path, wanted[name], name, hits[:3]))

    report.sort(reverse=True)
    print(f"\ndeclarations with evidence of a differently-named counterpart: {len(report)}")
    for count, rel_path, line, name, hits in report[:60]:
        print(f"  {count:3d}  {rel_path.split(chr(92))[-1]}:{line} {name}  <-  {', '.join(hits)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
