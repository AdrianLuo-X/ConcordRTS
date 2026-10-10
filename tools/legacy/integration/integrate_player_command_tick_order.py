"""Apply the reviewed command-tick ordering and grinding clock fixes."""
from pathlib import Path
import sys

path = Path(__file__).resolve().parents[3] / "src" / "runtime" / "host" / "RuntimeShell.cpp"
baseline = path.read_bytes()
newline = "\r\n" if b"\r\n" in baseline else "\n"
bom = baseline.startswith(b"\xef\xbb\xbf")
source = baseline.decode("utf-8-sig").replace("\r\n", "\n")

def replace_exact(old, new, label):
    global source
    count = source.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    source = source.replace(old, new, 1)

replace_exact(
    "    bool changed = updateRuntimeVisionState(state) || evaSubtitleChanged;\n"
    "    changed = advanceRuntimePlayerCommands(state) || changed;\n",
    "    bool changed = updateRuntimeVisionState(state) || evaSubtitleChanged;\n",
    "remove early player command step",
)
replace_exact(
    "    changed = advanceRuntimeSpies(state) || changed;\n"
    "    const std::size_t selectionSizeBeforeClean =\n",
    "    changed = advanceRuntimeSpies(state) || changed;\n"
    "    // Expiry and temporary activity cleanup must precede queued orders:\n"
    "    // a finished attack may leave camouflage bookkeeping until effects tick.\n"
    "    changed = advanceRuntimePlayerCommands(state) || changed;\n"
    "    const std::size_t selectionSizeBeforeClean =\n",
    "insert player commands after effects and status updates",
)
replace_exact(
    "void seedRuntimeGrindingTransactionScratch(\n"
    "    RuntimeWindowState& scratch,\n"
    "    const RuntimeWindowState& live\n"
    ") {\n"
    "    seedRuntimeLoadScratch(scratch, live, live.visionEnabled);\n",
    "void seedRuntimeGrindingTransactionScratch(\n"
    "    RuntimeWindowState& scratch,\n"
    "    const RuntimeWindowState& live\n"
    ") {\n"
    "    seedRuntimeLoadScratch(scratch, live, live.visionEnabled);\n"
    "    scratch.tick = live.tick;\n",
    "preserve grinding transaction tick",
)
print(f"Prepared command tick ordering and grinding clock for {path}")
if "--apply" in sys.argv:
    if path.read_bytes() != baseline:
        raise SystemExit("RuntimeShell changed during integration; retry")
    output = source.replace("\n", newline).encode("utf-8")
    path.write_bytes((b"\xef\xbb\xbf" if bom else b"") + output)
    print("Applied command ordering and grinding clock fixes.")
else:
    print("Dry run only; pass --apply to write.")
