"""Keep the special tick active while transient activity/recovery records exist."""
from pathlib import Path
import sys

path = Path(__file__).resolve().parents[3] / "src" / "runtime" / "host" / "RuntimeShell.cpp"
baseline = path.read_bytes()
newline = "\r\n" if b"\r\n" in baseline else "\n"
bom = baseline.startswith(b"\xef\xbb\xbf")
source = baseline.decode("utf-8-sig").replace("\r\n", "\n")
old = """    if (!hasSpecialCapableObject
        && state.specialMechanisms.mindControlLinks().empty()
        && state.specialMechanisms.radiationAreas().empty()
"""
new = """    if (!hasSpecialCapableObject
        && state.specialMechanisms.mindControlLinks().empty()
        && state.specialMechanisms.camouflageStates().empty()
        && !state.specialMechanisms.hasActiveChronoTemporalStates()
        && state.specialMechanisms.radiationAreas().empty()
"""
count = source.count(old)
if count != 1:
    raise SystemExit(f"special activity guard: expected one anchor, found {count}")
source = source.replace(old, new, 1)
print(f"Prepared special activity tick guard for {path}")
if "--apply" in sys.argv:
    if path.read_bytes() != baseline:
        raise SystemExit("RuntimeShell changed during integration; retry")
    output = source.replace("\n", newline).encode("utf-8")
    path.write_bytes((b"\xef\xbb\xbf" if bom else b"") + output)
    print("Applied special activity tick guard.")
else:
    print("Dry run only; pass --apply to write.")
