"""Wire the nonempty combat/player-command v15 probe; --check never writes."""
from pathlib import Path
import sys

target = Path(__file__).resolve().parents[3] / "src" / "runtime" / "host" / "RuntimeShell.cpp"
edits = [
    ("include after snapshot fixture helpers", "int runRuntimeMultiplayerSnapshotV15SmokeTest() {", '#include "network/snapshots/diagnostics/RuntimeDetailSnapshotSmoke.inl"\n\nint runRuntimeMultiplayerSnapshotV15SmokeTest() {'),
    ("invoke nonempty full snapshot probe", '''        std::cerr << "Multiplayer snapshot v15 smoke failed: "
                  << reason << "\\n";
        return 1;
    };
    auto source = std::make_unique<RuntimeWindowState>();''', '''        std::cerr << "Multiplayer snapshot v15 smoke failed: "
                  << reason << "\\n";
        return 1;
    };
    std::string detailSnapshotError;
    if (!runRuntimeDetailSnapshotSmokeProbe(detailSnapshotError)) return fail(detailSnapshotError);
    auto source = std::make_unique<RuntimeWindowState>();'''),
]
data = target.read_bytes()
had_bom = data.startswith(b"\xef\xbb\xbf")
newline = "\r\n" if b"\r\n" in data else "\n"
text = data.decode("utf-8-sig").replace("\r\n", "\n")
for name, old, new in edits:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{name}: expected 1 match, found {count}; no files written")
    text = text.replace(old, new)
if "--check" in sys.argv:
    print(f"{len(edits)} exact nonempty snapshot edits verified; no files written")
else:
    if target.read_bytes() != data:
        raise SystemExit("RuntimeShell changed during integration; no files written")
    encoded = text.replace("\n", newline).encode("utf-8")
    target.write_bytes((b"\xef\xbb\xbf" if had_bom else b"") + encoded)
    print(f"{len(edits)} nonempty snapshot edits applied")
