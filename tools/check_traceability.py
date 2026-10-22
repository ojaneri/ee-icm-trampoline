#!/usr/bin/env python3
"""Requirements <-> tests traceability.

Osvaldo Janeri Filho <janeri@gmail.com>

Every requirement in requirements/icm_requirements.md (ICM-F-xx, ICM-T-xx,
ICM-S-xx, ICM-D-xx) must be named by at least one test, and every
requirement a test names must exist. Tests name requirements in their
function names (C: test_ICM_S_02_..., Python: test_ICM_T_07_...) or in their
docstrings / comments ("ICM-S-02"). A range in a name, ICM_F_01_to_04,
counts for every requirement in it.

Prints the matrix as markdown with --matrix (pasted into the report).
Exit status 0 = complete, 1 = gaps.
"""
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REQ_FILE = ROOT / "requirements" / "icm_requirements.md"
REQ_DEF = re.compile(r"^\*\*(ICM-[FTSD]-\d\db?)\*\*", re.M)
REF_DASH = re.compile(r"ICM-([FTSD])-(\d\d)(b?)")
REF_NAME = re.compile(r"ICM_([FTSD])_(\d\d)(?:_to_(\d\d))?")


def test_files():
    yield from (ROOT / "tests" / "unit").glob("test_*.c")
    yield from (ROOT / "tests" / "integration").glob("test_*.py")


def references(text):
    refs = set()
    for kind, num, b in REF_DASH.findall(text):
        refs.add(f"ICM-{kind}-{num}{b}")
    for kind, lo, hi in REF_NAME.findall(text):
        for n in range(int(lo), int(hi or lo) + 1):
            refs.add(f"ICM-{kind}-{n:02d}")
    return refs


def tests_in(path):
    """(test name, text) pairs: the text of each test function."""
    src = path.read_text()
    pat = r"\n(?:static void|    def) (test_\w+)\(.*?(?=\n(?:static void|    def|int main|class |if __name__)|\Z)"
    for m in re.finditer(pat, src, re.S):
        yield m.group(1), m.group(0)


def main(argv):
    reqs = REQ_DEF.findall(REQ_FILE.read_text())
    covered = defaultdict(list)
    unknown = []
    for path in test_files():
        for name, text in tests_in(path):
            # C tests carry their requirement in the name; the comment above
            # a C function belongs to it, but the regex would hand it to the
            # previous one. Python tests carry it in the docstring.
            scope = name if path.suffix == ".c" else name + "\n" + text
            for ref in references(scope):
                if ref in reqs:
                    covered[ref].append(f"{path.stem}::{name}")
                elif ref.rstrip("b") not in reqs:
                    unknown.append(f"{path.name}::{name} names {ref}, which does not exist")

    missing = [r for r in reqs if r not in covered]
    if "--matrix" in argv:
        print("| Requirement | Tests |\n|---|---|")
        for r in reqs:
            print(f"| {r} | " + "<br>".join(sorted(set(covered.get(r, ["**none**"])))) + " |")
    for u in unknown:
        print("UNKNOWN:", u)
    for m in missing:
        print("UNCOVERED:", m)
    if not missing and not unknown:
        print(f"traceability: {len(reqs)} requirements, all covered; "
              f"no test names a requirement that does not exist")
    return 1 if (missing or unknown) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
