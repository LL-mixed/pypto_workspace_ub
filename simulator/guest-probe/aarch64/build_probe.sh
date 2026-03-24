#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$ROOT_DIR/out"
SRC="$ROOT_DIR/linqu_ub_probe.S"
OBJ="$OUT_DIR/linqu_ub_probe.o"
MACHO="$OUT_DIR/linqu_ub_probe.macho"
BIN="$OUT_DIR/linqu_ub_probe.bin"
LOAD_ADDR_FILE="$OUT_DIR/linqu_ub_probe.loadaddr"

mkdir -p "$OUT_DIR"

clang -arch arm64 -c "$SRC" -o "$OBJ"
ld -arch arm64 -e _start -static -pagezero_size 0 -segaddr __TEXT 0x40200000 -o "$MACHO" "$OBJ"

python3 - "$MACHO" "$BIN" "$LOAD_ADDR_FILE" <<'PY'
import re
import subprocess
import sys
from pathlib import Path

macho = Path(sys.argv[1])
bin_out = Path(sys.argv[2])
addr_out = Path(sys.argv[3])
text = subprocess.check_output(["otool", "-l", str(macho)], text=True)

section_re = re.compile(
    r"sectname (?P<sect>\S+)\s+segname (?P<seg>\S+)\s+addr (?P<addr>0x[0-9a-fA-F]+)\s+"
    r"size (?P<size>0x[0-9a-fA-F]+)\s+offset (?P<offset>\d+)",
    re.S,
)
sections = []
for m in section_re.finditer(text):
    if m.group("seg") != "__TEXT":
        continue
    sections.append(
        {
            "sect": m.group("sect"),
            "addr": int(m.group("addr"), 16),
            "size": int(m.group("size"), 16),
            "offset": int(m.group("offset")),
        }
    )

text_sec = next((s for s in sections if s["sect"] == "__text"), None)
if text_sec is None:
    raise SystemExit("failed to find __TEXT/__text section")

used = [s for s in sections if s["addr"] >= text_sec["addr"] and s["size"] > 0]
if not used:
    raise SystemExit("failed to find used __TEXT sections")

blob_start = min(s["offset"] for s in used)
blob_end = max(s["offset"] + s["size"] for s in used)
data = macho.read_bytes()[blob_start:blob_end]

bin_out.write_bytes(data)
addr_out.write_text(hex(text_sec["addr"]) + "\n")
print(bin_out)
print(addr_out)
PY

echo "built $BIN"
