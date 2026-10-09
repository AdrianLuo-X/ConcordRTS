"""Root-only, checked integration of the runtime feature-detail smoke probe."""
from pathlib import Path
import sys

target = Path(__file__).resolve().parents[1] / "src" / "RuntimeShell.cpp"
raw = target.read_bytes()
newline = "\r\n" if b"\r\n" in raw else "\n"
text = raw.decode("utf-8").replace("\r\n", "\n")
edits = [
    ('#include "RuntimeFactoryExitClearanceSmoke.inl"',
     '#include "RuntimeFactoryExitClearanceSmoke.inl"\n'
     '#include "../tests/RuntimeFeatureDetailIntegrationSmoke.inl"'),
    ('    std::string factoryExitError;\n'
     '    if (!runRuntimeFactoryExitClearanceSmokeProbe(state, factoryExitError)) {',
     '    std::string featureDetailError;\n'
     '    if (!runRuntimeFeatureDetailIntegrationSmokeProbe(state, featureDetailError)) {\n'
     '        std::cerr << "Command smoke failed: " << featureDetailError << \'\\n\';\n'
     '        return 1;\n'
     '    }\n'
     '    if (std::getenv("CONCORD_COMMAND_SMOKE_FEATURE_DETAILS_ONLY") != nullptr) return 0;\n'
     '    std::string factoryExitError;\n'
     '    if (!runRuntimeFactoryExitClearanceSmokeProbe(state, factoryExitError)) {'),
]
for old, new in edits:
    if text.count(old) != 1:
        raise SystemExit(f"Expected one anchor: {old[:100]!r}; no file written")
    text = text.replace(old, new)
if "--apply" in sys.argv:
    target.write_bytes(text.replace("\n", newline).encode("utf-8"))
    print("Applied runtime feature-detail smoke hooks")
else:
    print("All runtime feature-detail smoke hooks validated; pass --apply to write")
