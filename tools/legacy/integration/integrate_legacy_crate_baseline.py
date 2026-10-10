"""Root-only exact delta: preserve the legacy crate seed through late load staging.

The default also expands every previously hidden legacy-transient assertion
operand. Use --diagnostics-only to stage the diagnostic change alone. Nothing is
written until --apply is supplied, and all requested anchors are checked first.
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[3]
target = root / "src/runtime/host/RuntimeShell.cpp"
edits = []
if "--diagnostics-only" not in sys.argv:
    edits.append((
        "    lateCandidate.campaignMode = state.campaignMode;\n",
        "    lateCandidate.campaignMode = state.campaignMode;\n"
        "    // A save predating [Crates] restores the map's initial crate seed.\n"
        "    // Carry that immutable baseline through the nested load candidate.\n"
        "    lateCandidate.initialSkirmishCrates = state.initialSkirmishCrates;\n",
    ))
edits.append((
    '''                  << " next=" << state.nextProjectileActorId
                  << " gattling=" << state.gattlingWeaponTimers.size()
                  << " crates=" << state.skirmishCrates.size()
                  << " history="
                  << state.economy.limitedBuildHistoryByOwner.size()
                  << "\\n";
''',
    '''                  << " next=" << state.nextProjectileActorId
                  << " expired=" << state.recentExpiredProjectiles.size()
                  << " impacts=" << state.impactEffects.size()
                  << " replayEvents=" << state.recentReplayEvents.size()
                  << " replayNext=" << state.nextReplayEventSequence
                  << " deathTombstones="
                  << state.deathWeapons.triggeredObjects().size()
                  << " gattling=" << state.gattlingWeaponTimers.size()
                  << " crates=" << state.skirmishCrates.size()
                  << " expectedCrates=" << expectedLegacyCrates.size()
                  << " cratesMatch=" << formatBool(crateVectorsMatch(
                         state.skirmishCrates, expectedLegacyCrates))
                  << " history="
                  << state.economy.limitedBuildHistoryByOwner.size()
                  << " summaryInFlight="
                  << state.actionSummary.projectilesInFlight
                  << "\\n";
''',
))
raw = target.read_bytes()
newline = "\r\n" if b"\r\n" in raw else "\n"
text = raw.decode("utf-8").replace("\r\n", "\n")
for old, new in edits:
    count = text.count(old)
    if count != 1:
        raise SystemExit(
            f"Expected one anchor, found {count}: {old[:100]!r}; no files written"
        )
    text = text.replace(old, new)
if "--apply" in sys.argv:
    if target.read_bytes() != raw:
        raise SystemExit("RuntimeShell changed during integration; no files written")
    target.write_bytes(text.replace("\n", newline).encode("utf-8"))
    print(f"Applied {len(edits)} legacy crate/diagnostic deltas")
else:
    print(f"Validated {len(edits)} deltas; pass --apply to write")
