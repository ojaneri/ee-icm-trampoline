#!/usr/bin/env python3
"""Static check: ICM_Data is reachable only through icm_data.c.

Osvaldo Janeri Filho <janeri@gmail.com>

The race-freedom argument in requirements/architecture.md rests on three
facts about the source, which this script checks on every CI run so they
cannot quietly stop being true:

1. The instance (`static ICM_Data icm_data`) is defined in icm_data.c and
   nowhere else, and no other file mentions `icm_data.` or `&icm_data`.
2. GetResource(ICM_Resource) / ReleaseResource(ICM_Resource) appear only in
   icm_data.c (inside the ICM_LOCK / ICM_UNLOCK macros), so no caller can
   hold the resource across its own code.
3. In every function of icm_data.c that takes the lock, ICM_LOCK() and
   ICM_UNLOCK() are equal in number, and no `return` sits between them.

And for the OIL side: every task that calls an icm_data_* accessor lists
RESOURCE = ICM_Resource in src/icm.oil (otherwise the ceiling computed by
GOIL would be too low and EXTENDED status would reject the call at run time).

Exit status 0 = all good, 1 = violation (printed).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def strip_comments(code: str) -> str:
    """Comments and #include lines out: `#include "icm_data.h"` is not an
    access to `icm_data.` (the first version of this check said it was)."""
    code = re.sub(r"/\*.*?\*/", "", code, flags=re.S)
    code = re.sub(r"//[^\n]*", "", code)
    return re.sub(r"^\s*#\s*include[^\n]*", "", code, flags=re.M)


def functions(code: str):
    """Yields (name, body) of top-level C functions (good enough for our code)."""
    for m in re.finditer(r"\n[\w \*]+?\b(\w+)\s*\([^;{]*\)\s*\{", code):
        start = m.end()
        depth = 1
        i = start
        while depth and i < len(code):
            depth += {"{": 1, "}": -1}.get(code[i], 0)
            i += 1
        yield m.group(1), code[start:i - 1]


def task_bodies(code: str):
    for m in re.finditer(r"\bTASK\((\w+)\)\s*\{", code):
        start = m.end()
        depth, i = 1, start
        while depth and i < len(code):
            depth += {"{": 1, "}": -1}.get(code[i], 0)
            i += 1
        yield m.group(1), code[start:i - 1]


def main() -> int:
    errors = []
    sources = {p.name: strip_comments(p.read_text()) for p in SRC.glob("*.c")}

    # 1. the instance
    for name, code in sources.items():
        if name == "icm_data.c":
            if len(re.findall(r"\bstatic\s+ICM_Data\s+icm_data\s*;", code)) != 1:
                errors.append("icm_data.c: expected exactly one 'static ICM_Data icm_data;'")
            continue
        if re.search(r"\bicm_data\s*\.|&\s*icm_data\b|\bICM_Data\s+icm_data\b", code):
            errors.append(f"{name}: touches the ICM_Data instance directly")

    # 2. the resource
    for name, code in sources.items():
        if name != "icm_data.c" and re.search(r"(Get|Release)Resource\s*\(\s*ICM_Resource", code):
            errors.append(f"{name}: takes ICM_Resource itself; use the icm_data_* accessors")

    # 3. balanced locking in every accessor
    for fname, body in functions(sources["icm_data.c"]):
        locks, unlocks = body.count("ICM_LOCK()"), body.count("ICM_UNLOCK()")
        if locks != unlocks:
            errors.append(f"icm_data.c:{fname}: {locks} ICM_LOCK vs {unlocks} ICM_UNLOCK")
        if locks:
            inside = body[body.find("ICM_LOCK()"):body.rfind("ICM_UNLOCK()")]
            if re.search(r"\breturn\b", inside):
                errors.append(f"icm_data.c:{fname}: return while holding ICM_Resource")

    # 4. OIL: every task calling an accessor declares the resource
    oil = (SRC / "icm.oil").read_text()
    for task, body in task_bodies(sources["icm_app.c"]):
        uses = re.search(r"\bicm_data_\w+\s*\(", body)
        helper_uses = re.search(r"\bhandle_frame\s*\(", body)  # Task_CAN_RX helper
        if not (uses or helper_uses):
            continue
        # up to the TASK's own closing "  };" (nested AUTOSTART blocks end in
        # "};" too, so a plain non-greedy match would stop inside them)
        block = re.search(rf"TASK\s+{task}\s*\{{(.*?)\n  \}};", oil, re.S)
        if not block or "RESOURCE = ICM_Resource" not in block.group(1):
            errors.append(f"icm.oil: {task} uses ICM_Data but does not list ICM_Resource")

    for e in errors:
        print("VIOLATION:", e)
    if not errors:
        print("shared-access check: ICM_Data is only reachable through icm_data.c "
              "accessors; every accessor is balanced; every user task declares "
              "ICM_Resource in icm.oil")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
