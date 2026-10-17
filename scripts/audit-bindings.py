#!/usr/bin/env python3
"""Ensure every required Windows protection/lifecycle hook has a real address.
This is a source-compatibility check, not proof of hook installation in the game.
"""
from pathlib import Path
import re
import sys

bro = Path(sys.argv[1]).read_text(encoding="utf-8")
root = Path(__file__).resolve().parent.parent
source = (root / "src/SafetyHooks.cpp").read_text(encoding="utf-8")
required = source.split("constexpr auto required", 1)[1].split("});", 1)[0]
hooks = re.findall(r'"([A-Za-z0-9_]+)::([A-Za-z0-9_]+)"', required)
classes = {}
current = None
for line in bro.splitlines():
    start = re.match(r"^class (\w+)\b", line)
    if start:
        current = start[1]
        classes[current] = []
    if current:
        classes[current].append(line)
failures = []
for name, method in hooks:
    declarations = [line for line in classes.get(name, [])
                    if re.search(r"\b" + re.escape(method) + r"\(", line)]
    if not any(re.search(r"\bwin\s+0x[0-9a-fA-F]+", line) for line in declarations):
        failures.append(f"{name}::{method}: no Windows address (missing/inline)")
if failures:
    raise SystemExit("\n".join(failures))
assert hooks, "No safety hooks found"
print(f"OK: {len(hooks)} required hooks have explicit GD 2.2081 Windows addresses")
