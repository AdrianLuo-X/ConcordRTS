"""Preserve immediate local UI selection changes made by repack commands."""
from pathlib import Path
import sys

path = Path(__file__).resolve().parents[1] / "src" / "RuntimeShell.cpp"
baseline = path.read_bytes()
newline = "\r\n" if b"\r\n" in baseline else "\n"
bom = baseline.startswith(b"\xef\xbb\xbf")
source = baseline.decode("utf-8-sig").replace("\r\n", "\n")
old = "    const bool accepted = applyRuntimePlayerTargetOrder(state, state.economy.owner, body);\n"
new = "    const bool accepted = applyRuntimePlayerTargetOrder(state, state.economy.owner, body, true);\n"
count = source.count(old)
if count != 1:
    raise SystemExit(f"immediate player UI dispatch: expected one anchor, found {count}")
source = source.replace(old, new, 1)
print(f"Prepared immediate UI selection preservation for {path}")
if "--apply" in sys.argv:
    if path.read_bytes() != baseline:
        raise SystemExit("RuntimeShell changed during integration; retry")
    output = source.replace("\n", newline).encode("utf-8")
    path.write_bytes((b"\xef\xbb\xbf" if bom else b"") + output)
    print("Applied immediate UI selection preservation.")
else:
    print("Dry run only; pass --apply to write.")
