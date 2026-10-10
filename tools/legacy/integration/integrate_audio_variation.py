"""Root-only exact replacements for the shared RuntimeShell.cpp. Default: dry run."""
from pathlib import Path
import sys

target = Path(__file__).resolve().parents[3] / "src" / "runtime" / "host" / "RuntimeShell.cpp"
raw = target.read_bytes()
newline = "\r\n" if b"\r\n" in raw else "\n"
text = raw.decode("utf-8").replace("\r\n", "\n")
edits = [
    ('#include "audio/backend/RuntimeSfxChannelPoolMci.inl"',
     '#include "audio/backend/RuntimeSfxChannelPoolMci.inl"\n#include "audio/spatial/RuntimeAudioVariation.inl"', 1),
    ('    if (record.eva && !record.playable && record.subtitleText.empty()) {',
     '    applyRuntimeAudioVariation(state, record);\n\n'
     '    if (record.eva && !record.playable && record.subtitleText.empty()) {', 1),
    ('    const std::optional<std::string> samplePath =\n'
     '        findRuntimeSoundSamplePath(state, action.sampleName);\n'
     '    closeRuntimeWorldAudioMciHandle(action.handleId);',
     '    std::optional<std::string> samplePath =\n'
     '        findRuntimeSoundSamplePath(state, action.sampleName);\n'
     '    if (samplePath.has_value()) {\n'
     '        samplePath = runtimeAudioPitchVariantPath(*samplePath,\n'
     '            std::filesystem::path(state.config.localDataRootPath) / "runtime_audio_cache",\n'
     '            action.frequencyPermille);\n'
     '    }\n'
     '    closeRuntimeWorldAudioMciHandle(action.handleId);', 1),
    ('        output << "CatalogHash=" << state.worldAudioCatalog.stateHash()',
     '        output << "CatalogVersion=2\\n";\n'
     '        output << "CatalogHash=" << state.worldAudioCatalog.stateHash()', 1),
    ('            || savedCatalogHash != state.worldAudioCatalog.stateHash()',
     '            || (savedCatalogHash != state.worldAudioCatalog.stateHash()\n'
     '                && (ini.getValue("RuntimeWorldAudioHost", "CatalogVersion", "1") != "1"\n'
     '                    || savedCatalogHash != state.worldAudioCatalog.stateHash(false)))', 2),
]
for old, new, expected in edits:
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"Anchor count {count} != {expected}: {old[:100]!r}; no file written")
    text = text.replace(old, new)

if "--apply" in sys.argv:
    # Every anchor was validated before touching the shared file.
    target.write_bytes(text.replace("\n", newline).encode("utf-8"))
    print("Applied all audio variation production hooks to RuntimeShell.cpp")
else:
    print("Validated all audio variation hooks; rerun with --apply to write")
