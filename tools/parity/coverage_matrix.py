#!/usr/bin/env python3
"""Build the per-function coverage matrix for the port.

Reads the lexically extracted declaration lists produced by `inventory.py` and emits one
markdown row per reference declaration: the reference file and line, the declaration name,
the port symbol(s) that were matched to it, and the verification status recorded by the
hand-written parity reports under `docs/parity/`.

Matching is deliberately conservative. A reference name is linked to a port symbol only when
the names agree after normalisation (case, leading underscore, common verb prefixes such as
`get`/`set`, and Java-specific suffixes such as `Sec`/`Ms`); everything else is reported as
unmatched so a human has to look at it. The matrix is therefore an index into the report, not
a substitute for it.

Status values:
  verified    a parity report names the reference declaration and states MATCH
  fixed       a parity report names it and states what was changed
  excluded    a parity report records a deliberate exclusion with a reason
  unmatched   no port symbol matched by name; needs a manual verdict
"""
import argparse
import json
import os
import re
import sys

PREFIXES = ("get", "set", "is", "has", "add", "apply", "update", "compute", "parse", "read")
SUFFIXES = ("sec", "ms", "px", "norm")
REPORT_FILES = ("chart.md", "core.md", "judging.md", "render.md", "ui.md",
                "gameplay.md", "platform.md", "audio.md")


def normalise(name: str) -> str:
    """Lower-case a declaration name and drop decorations that differ by language."""
    value = name.strip().lstrip("_~")
    value = re.sub(r"^[A-Za-z]+(?=::)", "", value)  # drop a qualifier
    value = value.split("::")[-1]
    value = re.sub(r"[^A-Za-z0-9]", "", value).lower()
    return value


def variants(name: str) -> set:
    """All spellings under which the same member plausibly appears on the other side."""
    base = normalise(name)
    out = {base}
    for prefix in PREFIXES:
        if base.startswith(prefix) and len(base) > len(prefix):
            out.add(base[len(prefix):])
    for suffix in SUFFIXES:
        if base.endswith(suffix) and len(base) > len(suffix):
            out.add(base[: -len(suffix)])
    return {v for v in out if v}


def load_declarations(path: str):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def load_report_verdicts(directory: str):
    """Collect (reference location -> verdict) pairs recorded by the parity reports."""
    verdicts = []
    if not os.path.isdir(directory):
        return verdicts
    for name in REPORT_FILES:
        path = os.path.join(directory, name)
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as handle:
            for raw in handle:
                line = raw.strip()
                if not line.startswith("|") or set(line) <= set("|- "):
                    continue
                cells = [cell.strip() for cell in line.strip("|").split("|")]
                if len(cells) < 3:
                    continue
                joined = " ".join(cells).lower()
                if "reference" in joined and "verdict" in joined:
                    continue  # header row
                # Only a table row whose verdict cell carries a verdict word counts. Matching
                # anywhere in the row picks up prose ("a remaining difference", "missing
                # asset") and invents verdicts that the report never gave.
                verdict = None
                for cell in cells:
                    text = cell.strip().lower().strip("*")
                    if not text:
                        continue
                    if text.startswith("fixed"):
                        verdict = "fixed"
                        break
                    if text.startswith(("match", "matches")):
                        verdict = "match"
                        break
                    if text.startswith(("exclud", "port-only")):
                        verdict = "excluded"
                        break
                    if text.startswith(("differs", "missing")):
                        verdict = "differs"
                        break
                verdicts.append((name, cells, verdict))
    return verdicts


def classify(name: str, verdicts):
    key = normalise(name)
    found = set()
    for _report, cells, verdict in verdicts:
        if verdict is None:
            continue
        row = " ".join(cells).lower()
        if key and key in normalise(row):
            if verdict.startswith("fixed"):
                found.add("fixed")
            elif verdict.startswith(("exclud", "port-only")):
                found.add("excluded")
            elif verdict.startswith(("match", "matches")):
                found.add("verified")
            else:
                found.add("differs")
    if "fixed" in found:
        return "fixed"
    if "differs" in found:
        return "differs"
    if "verified" in found and "excluded" not in found:
        return "verified"
    if "excluded" in found:
        return "excluded"
    return ""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--inventory", default="docs/compare/inventory.json")
    parser.add_argument("--reports", default="docs/parity")
    parser.add_argument("--out", default="docs/parity/coverage-matrix.md")
    args = parser.parse_args()

    data = load_declarations(args.inventory)
    verdicts = load_report_verdicts(args.reports)

    # Index every port declaration under all of its plausible spellings.
    port_index = {}
    for path, decls in data["cpp"].items():
        for decl in decls:
            for variant in variants(decl["name"]):
                port_index.setdefault(variant, set()).add((path, decl["line"], decl["name"]))

    rows = []
    counts = {"verified": 0, "fixed": 0, "excluded": 0, "differs": 0, "unmatched": 0,
              "matched-by-name": 0}
    for path in sorted(data["java"]):
        for decl in data["java"][path]:
            name = decl["name"]
            matched = set()
            for variant in variants(name):
                matched.update(port_index.get(variant, ()))
            status = classify(name, verdicts)
            # A report verdict always wins. Without one, a port symbol with the same
            # normalised name is only a candidate correspondence, never a verified one; the
            # two are reported separately so the matrix never overstates coverage.
            if not status:
                status = "matched-by-name" if matched else "unmatched"
            counts[status if status in counts else "unmatched"] = \
                counts.get(status if status in counts else "unmatched", 0) + 1
            target = ""
            if matched:
                first = sorted(matched, key=lambda item: (item[0], item[1]))[0]
                target = f"`{first[0]}:{first[1]}` `{first[2]}`"
                if len(matched) > 1:
                    target += f" (+{len(matched) - 1})"
            rows.append((path, decl["line"], name, status, target))

    total = sum(counts.values()) + counts.get("matched-by-name", 0)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as handle:
        handle.write("# Per-function coverage matrix\n\n")
        handle.write("Generated from `docs/compare/inventory.json` by "
                     "`tools/parity/coverage_matrix.py`. One row per reference declaration.\n\n")
        handle.write(f"Reference declarations: **{total}** across "
                     f"{len(data['java'])} files.\n\n")
        handle.write("| Status | Count | Meaning |\n| --- | --- | --- |\n")
        handle.write(f"| verified | {counts.get('verified', 0)} | named MATCH by a parity report |\n")
        handle.write(f"| fixed | {counts.get('fixed', 0)} | named by a parity report with a fix |\n")
        handle.write(f"| differs | {counts.get('differs', 0)} | named with a remaining difference |\n")
        handle.write(f"| excluded | {counts.get('excluded', 0)} | deliberate exclusion, reason recorded |\n")
        handle.write(f"| matched-by-name | {counts.get('matched-by-name', 0)} | a port symbol with the same name exists |\n")
        handle.write(f"| unmatched | {counts.get('unmatched', 0)} | no same-name port symbol; needs a manual verdict |\n\n")
        handle.write("This matrix is an index. The authoritative per-function verdicts and the\n")
        handle.write("evidence line for each one are in the topical reports "
                     "(`chart.md`, `core.md`, `judging.md`,\n`render.md`, `ui.md`, "
                     "`gameplay.md`, `platform.md`, `audio.md`).\n\n")

        current = None
        for path, line, name, status, target in rows:
            if path != current:
                current = path
                handle.write(f"\n## `{path}`\n\n")
                handle.write("| Line | Reference declaration | Status | Port symbol |\n")
                handle.write("| --- | --- | --- | --- |\n")
            handle.write(f"| {line} | `{name}` | {status} | {target} |\n")

    print(f"rows={total} verified={counts.get('verified', 0)} fixed={counts.get('fixed', 0)} "
          f"differs={counts.get('differs', 0)} excluded={counts.get('excluded', 0)} "
          f"matched-by-name={counts.get('matched-by-name', 0)} unmatched={counts.get('unmatched', 0)}")
    print(f"written {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
