"""Root-only exact delta: verify Dryheat's real slave Shovel draw, not vehicle OREGATH."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[3]
shell_old = '''        for (const auto& [workerId, task] : state.slaveMinerTasks) {
            (void)workerId;
            slaveWorkerMaxCargoObserved =
                std::max(slaveWorkerMaxCargoObserved, task.cargoUnits);
        }
'''
shell_new = '''        for (const auto& [workerId, task] : state.slaveMinerTasks) {
            slaveWorkerMaxCargoObserved =
                std::max(slaveWorkerMaxCargoObserved, task.cargoUnits);
            // Sample the actual digging frame while it is active. SLAV uses
            // Shovel, whereas OREGATH is exclusively the vehicle intake.
            const auto* worker = findObjectById(state, workerId);
            if (!slaveMinerGatherDrawn && worker != nullptr
                && worker->owner == state.economy.owner
                && worker->mission == "HarvestLoad" && !worker->renderMoveActive) {
                const auto gatherStats = drawRuntimeStateOffscreenAtCell(
                    state, runtimeCellPointForObject(*worker), 320, 240,
                    slaveMinerRenderError, "", false);
                slaveMinerGatherDrawn = gatherStats.has_value()
                    && gatherStats->sequenceShovelHits > 0;
            }
        }
'''
obsolete_probe = '''    for (const RuntimeImpactEffect& impact : state.impactEffects) {
        if (impact.renderIntent != "oreGather" || impact.animId != "OREGATH") {
            continue;
        }
        std::string slaveMinerRenderError;
        const std::optional<RuntimeMapRenderStats> slaveMinerGatherStats =
            drawRuntimeStateOffscreenAtCell(
                state,
                impact.cell,
                320,
                240,
                slaveMinerRenderError,
                "",
                false
            );
        slaveMinerGatherDrawn =
            slaveMinerGatherStats.has_value()
            && slaveMinerGatherStats->impactActorDrawn > 0;
        break;
    }
'''
changes = {
    "src/render/core/RuntimeMapRenderer.h": [
        ("    int sequenceWalkHits = 0;\n", "    int sequenceWalkHits = 0;\n    int sequenceShovelHits = 0;\n"),
    ],
    "src/render/core/RuntimeMapRenderer.cpp": [
        ('''    if (sequence.find("fire") != std::string::npos
        || sequence.find("attack") != std::string::npos) {
        ++stats.sequenceFireHits;
    }
''', '''    if (sequence == "shovel") {
        ++stats.sequenceShovelHits;
    }
    if (sequence.find("fire") != std::string::npos
        || sequence.find("attack") != std::string::npos) {
        ++stats.sequenceFireHits;
    }
'''),
    ],
    "src/runtime/host/RuntimeShell.cpp": [
        ("    bool slaveMinerGatherDrawn = false;\n", "    bool slaveMinerGatherDrawn = false;\n    std::string slaveMinerRenderError;\n"),
        (shell_old, shell_new),
        (obsolete_probe, ""),
        ('''                  << " drawn=" << formatBool(slaveMinerGatherDrawn)
                  << " interval=" << slaveMinerInterval
''', '''                  << " drawn=" << formatBool(slaveMinerGatherDrawn)
                  << " renderError=" << formatMaybe(slaveMinerRenderError)
                  << " interval=" << slaveMinerInterval
'''),
    ],
}
staged = []
for name, edits in changes.items():
    target = root / name
    raw = target.read_bytes()
    newline = "\r\n" if b"\r\n" in raw else "\n"
    text = raw.decode("utf-8").replace("\r\n", "\n")
    for old, new in edits:
        count = text.count(old)
        if count != 1:
            raise SystemExit(f"Expected one anchor in {name}, found {count}: {old[:100]!r}; no files written")
        text = text.replace(old, new)
    staged.append((target, text.replace("\n", newline).encode("utf-8")))
if "--apply" in sys.argv:
    for target, content in staged:
        target.write_bytes(content)
    print("Applied real Shovel rendering counters and Dryheat capture assertion")
else:
    print("All three files validated; pass --apply to write")
