#!/usr/bin/env python3
"""Fail closed if the original Microsoft COMMap IDL differs by one byte."""
from pathlib import Path
import hashlib
import json
import sys

SOURCE_SHA = "dcf19ff69001c6f2f242d15c917a5598b03d70ba"
if len(sys.argv) != 2:
    raise SystemExit("usage: check_idl.py COMMap.idl")
data = Path(sys.argv[1]).read_bytes()
identity = hashlib.sha1(
    b"blob " + str(len(data)).encode("ascii") + b"\0" + data
).hexdigest()
if identity != SOURCE_SHA:
    raise SystemExit(f"MICROSOFT_COMMAP_IDL_ORIGINAL_FAIL {identity} != {SOURCE_SHA}")
print(f"MICROSOFT_COMMAP_IDL_ORIGINAL_OK blob={identity} bytes={len(data)}")

root = Path(sys.argv[1]).parent
manifest = json.loads((root / "original_sources.json").read_text())
for name, expected in manifest["files"].items():
    data = (root / name).read_bytes()
    actual = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()
    if actual != expected:
        raise SystemExit(f"MICROSOFT_COMMAP_SOURCE_ORIGINAL_FAIL {name}: {actual} != {expected}")
print(f"MICROSOFT_COMMAP_SOURCES_ORIGINAL_OK files={len(manifest['files'])}")
