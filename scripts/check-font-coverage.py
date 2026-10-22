"""Checks that every non-ASCII character the interface can display has a glyph.

The interface font covers a bounded set of code points. A character outside it renders as
an empty box, so any string the screens can draw is compared against the font's own
character map and the missing ones are reported.
"""

import glob
import re
import sys


def string_literals(path):
    """Yields the string literals of a C++ source file, with escapes decoded."""
    source = open(path, encoding="utf-8", errors="ignore").read()
    pattern = re.compile(r'"((?:[^"\\]|\\.)*)"')
    for match in pattern.finditer(source):
        # Decode only the escape sequences the compiler accepts, leaving text as UTF-8.
        text = match.group(1)
        text = text.replace("\\n", "\n").replace("\\t", "\t").replace("\\r", "\r")
        text = text.replace('\\\\', "\\").replace('\\"', '"')
        yield text


def main():
    font_path = "assets/res/font.ttf"
    try:
        from fontTools.ttLib import TTFont
    except ImportError:
        print("fontTools is not available; cannot check coverage")
        return 1
    font = TTFont(font_path, fontNumber=0)
    covered = set(font.getBestCmap().keys())

    used = set()
    for path in sorted(glob.glob("src/app/*.cpp")):
        for literal in string_literals(path):
            for character in literal:
                if ord(character) > 0x7F:
                    used.add(character)

    missing = sorted(c for c in used if ord(c) not in covered)
    print("non-ASCII characters used by the interface:", len(used))
    if not missing:
        print("all of them have a glyph")
        return 0
    print("missing a glyph:", len(missing))
    for character in missing:
        print("  U+%04X" % ord(character))
    return 1


if __name__ == "__main__":
    sys.exit(main())
