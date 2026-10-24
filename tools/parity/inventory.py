#!/usr/bin/env python3
"""Static inventory helpers for comparing the reference Android sources with this port.

Sub-commands
  inventory  list every method/function definition on both sides
  literals   report numeric literals that exist on the reference side only
  strings    report string literals that exist on the reference side only
  calls      list call sites per method (reference side) for traceability

The parser is deliberately lexical: it removes comments and literals, then walks
brace depth to recover declarations. It favours recall over precision because the
output is a review aid, not a compiler front-end.
"""
import argparse
import json
import os
import re
import sys
from collections import defaultdict

JAVA_ROOT = r"E:\Chigros\app\src\main\java"
CPP_ROOTS = [r"E:\ReChigros\src", r"E:\ReChigros\include", r"E:\ReChigros\platform"]

CONTROL = {
    "if", "for", "while", "switch", "catch", "synchronized", "return", "do",
    "else", "try", "finally", "case", "default", "new", "assert", "throw",
    "super", "this", "namespace", "extern", "sizeof", "decltype", "static_assert",
    "operator", "struct", "class", "enum", "union", "typedef", "using", "template",
}


def strip_code(text, java):
    """Blank out comments and string/char literals, keeping offsets and line breaks."""
    out = list(text)
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            for _ in range(2):
                if i < n:
                    out[i] = " "
                    i += 1
        elif c == '"':
            out[i] = " "
            i += 1
            while i < n and text[i] != '"':
                if text[i] == "\\":
                    out[i] = " "
                    i += 1
                    if i < n:
                        out[i] = " "
                        i += 1
                    continue
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                i += 1
        elif c == "'":
            if java:
                out[i] = " "
                i += 1
                while i < n and text[i] != "'":
                    if text[i] == "\\":
                        out[i] = " "
                        i += 1
                        if i < n:
                            out[i] = " "
                            i += 1
                        continue
                    if text[i] != "\n":
                        out[i] = " "
                    i += 1
                if i < n:
                    out[i] = " "
                    i += 1
            else:
                i += 1
        else:
            i += 1
    return "".join(out)


def line_of(text, offset):
    return text.count("\n", 0, offset) + 1


def declarations(path, text, java):
    """Return (name, signature, line, depth) for every brace-delimited declaration."""
    code = strip_code(text, java)
    results = []
    start = 0
    depth = 0
    i = 0
    n = len(code)
    while i < n:
        c = code[i]
        if c == "{" or c == "}" or c == ";":
            chunk = code[start:i]
            if c == "{" and "(" in chunk and ")" in chunk:
                tail = chunk.rstrip()
                if tail.endswith(("=", ",", "(")) or "->" in tail.split("(")[-1]:
                    pass
                tail = re.sub(r"\s+", " ", tail)
                m = re.search(
                    r"([A-Za-z_~][\w:~]*)\s*\((?:[^()]|\([^()]*\))*\)"
                    r"\s*(?:const\b|noexcept\b|override\b|final\b|mutable\b|throw\b|"
                    r"->[\w:<>,&\s\*]+|:[^{]*|\[[^\]]*\]|\s)*$",
                    tail,
                )
                if m:
                    name = m.group(1).split("::")[-1].lstrip("~")
                    if name and name not in CONTROL and not tail.endswith("typedef"):
                        sig = re.sub(r"\s+", " ", tail[-260:]).strip()
                        results.append((name, sig, line_of(text, start), depth))
            if c == "{":
                depth += 1
                start = i + 1
            elif c == "}":
                depth = max(0, depth - 1)
                start = i + 1
            else:
                start = i + 1
        i += 1
    return results


def iter_files(roots, exts, skip):
    for root in roots:
        for base, dirs, files in os.walk(root):
            dirs[:] = [d for d in dirs if d not in skip and not d.startswith(".cxx")]
            for f in files:
                if os.path.splitext(f)[1] in exts:
                    yield os.path.join(base, f)


SKIP = {"build", ".git", "third_party", ".gradle", ".idea", "artifacts", ".cxx",
        "node_modules", "test-scratch", "perf-final"}


def read(path):
    for enc in ("utf-8", "gbk", "latin-1"):
        try:
            with open(path, "r", encoding=enc) as fh:
                return fh.read()
        except UnicodeDecodeError:
            continue
    return ""


def cmd_inventory(args):
    out = {"java": {}, "cpp": {}}
    for path in iter_files([JAVA_ROOT], {".java"}, SKIP):
        text = read(path)
        out["java"][os.path.relpath(path, JAVA_ROOT)] = [
            {"name": n, "sig": s, "line": l, "depth": d} for n, s, l, d in declarations(path, text, True)
        ]
    for path in iter_files(CPP_ROOTS, {".cpp", ".hpp", ".h"}, SKIP):
        text = read(path)
        rel = os.path.relpath(path, r"E:\ReChigros")
        out["cpp"][rel] = [
            {"name": n, "sig": s, "line": l, "depth": d} for n, s, l, d in declarations(path, text, False)
        ]
    with open(args.out, "w", encoding="utf-8") as fh:
        json.dump(out, fh, indent=1, ensure_ascii=False)
    jt = sum(len(v) for v in out["java"].values())
    ct = sum(len(v) for v in out["cpp"].values())
    print(f"java files={len(out['java'])} decls={jt}")
    print(f"cpp  files={len(out['cpp'])} decls={ct}")
    # name coverage: reference names with no identical-name port symbol
    cpp_names = defaultdict(list)
    for f, decls in out["cpp"].items():
        for d in decls:
            cpp_names[d["name"].lower()].append(f)
    missing = []
    for f, decls in out["java"].items():
        for d in decls:
            if d["name"].lower() not in cpp_names:
                missing.append((f, d["line"], d["name"]))
    print(f"reference method names absent from the port by name: {len(missing)}")
    with open(args.out + ".names.txt", "w", encoding="utf-8") as fh:
        for f, l, n in missing:
            fh.write(f"{f}:{l}\t{n}\n")


NUM = re.compile(r"(?<![\w.])(\d+\.\d+|\d+)(?![\w.])")
TRIVIAL = {"0", "1", "2", "3", "4", "0.0", "1.0", "0.5", "255", "100", "1000", "10", "16", "8", "60", "24"}


def collect_literals(paths, java):
    per_file = {}
    for path in paths:
        text = read(path)
        code = strip_code(text, java)
        vals = defaultdict(int)
        for m in NUM.finditer(code):
            v = m.group(1)
            vals[v] += 1
        per_file[path] = vals
    return per_file


def cmd_literals(args):
    java_files = [p for p in iter_files([JAVA_ROOT], {".java"}, SKIP) if args.filter in p]
    cpp_files = list(iter_files(CPP_ROOTS, {".cpp", ".hpp", ".h"}, SKIP))
    jl = collect_literals(java_files, True)
    cl = collect_literals(cpp_files, False)
    HAVE = set()
    for vals in cl.values():
        HAVE.update(vals)
    rows = []
    for path, vals in jl.items():
        for v, c in vals.items():
            if v in TRIVIAL or v in HAVE:
                continue
            rows.append((os.path.relpath(path, JAVA_ROOT), v, c))
    rows.sort(key=lambda r: (r[0], r[1]))
    with open(args.out, "w", encoding="utf-8") as fh:
        for r in rows:
            fh.write(f"{r[0]}\t{r[1]}\t{r[2]}\n")
    print(f"numeric literals only on the reference side: {len(rows)} -> {args.out}")


STR = re.compile(r'"((?:[^"\\]|\\.)*)"')


def cmd_strings(args):
    java_files = [p for p in iter_files([JAVA_ROOT], {".java"}, SKIP) if args.filter in p]
    cpp_text = []
    for p in iter_files(CPP_ROOTS, {".cpp", ".hpp", ".h", ".glsl"}, SKIP):
        cpp_text.append(read(p))
        if p.endswith(".glsl"):
            cpp_text.append(read(p))
    blob = "\n".join(cpp_text)
    rows = []
    for path in java_files:
        text = read(path)
        for m in STR.finditer(text):
            s = m.group(1)
            if len(s) < 2 or s.startswith(("%", "\\n", "\\u")):
                continue
            if "\\" in s or s.strip() != s:
                continue
            if s in blob:
                continue
            rows.append((os.path.relpath(path, JAVA_ROOT), line_of(text, m.start()), s))
    with open(args.out, "w", encoding="utf-8") as fh:
        for r in rows:
            fh.write(f"{r[0]}:{r[1]}\t{r[2]}\n")
    print(f"string literals only on the reference side: {len(rows)} -> {args.out}")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("inventory")
    p.add_argument("--out", default="docs/compare/inventory.json")
    p.set_defaults(func=cmd_inventory)
    p = sub.add_parser("literals")
    p.add_argument("--filter", default="")
    p.add_argument("--out", default="docs/compare/literals_missing.txt")
    p.set_defaults(func=cmd_literals)
    p = sub.add_parser("strings")
    p.add_argument("--filter", default="")
    p.add_argument("--out", default="docs/compare/strings_missing.txt")
    p.set_defaults(func=cmd_strings)
    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
