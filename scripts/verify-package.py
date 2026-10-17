#!/usr/bin/env python3
"""Validate package integrity, Windows DLL and bundled font, not gameplay."""
import json
from pathlib import Path
import re
import struct
import sys
import zipfile

package = Path(sys.argv[1])
with zipfile.ZipFile(package) as archive:
    assert archive.testzip() is None, "ZIP CRC failure"
    metadata = json.loads(archive.read("mod.json"))
    assert metadata["id"] == "local.cube_learner"
    assert metadata["geode"] == "5.11.2"
    assert metadata["gd"] == {"win": "2.2081"}
    library = archive.read("local.cube_learner.dll")
    assert library[:2] == b"MZ", "Expected a Windows PE DLL"
    offset = struct.unpack_from("<I", library, 0x3c)[0]
    assert library[offset:offset + 4] == b"PE\0\0"
    assert struct.unpack_from("<H", library, offset + 4)[0] == 0x8664, "Expected x64"
    assert struct.unpack_from("<H", library, offset + 22)[0] & 0x2000, "Expected DLL"
    directory = "resources/local.cube_learner/"
    font = archive.read(directory + "bot.fnt").decode("utf-8")
    glyphs = {int(value) for value in re.findall(r"^char id=(\d+)", font, re.M)}
    assert all(code in glyphs for code in range(0x410, 0x450)), "Missing Cyrillic"
    assert {0x401, 0x451} <= glyphs, "Missing Yo glyphs"
    assert 'file="bot.png"' in font
    assert archive.read(directory + "bot.png")[:8] == b"\x89PNG\r\n\x1a\n"
    assert archive.read(directory + "FONT-LICENSE.txt")
print(f"OK: {package.name}: valid ZIP, GD 2.2081 / Geode 5.11.2, Windows x64 DLL, Cyrillic font")
