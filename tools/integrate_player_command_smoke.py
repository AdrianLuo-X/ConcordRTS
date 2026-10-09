"""Connect the real player command runtime probe to command smoke."""
from pathlib import Path
import sys

path = Path(__file__).resolve().parents[1] / 'src' / 'RuntimeShell.cpp'
baseline = path.read_bytes()
newline = '\r\n' if b'\r\n' in baseline else '\n'
bom = baseline.startswith(b'\xef\xbb\xbf')
source = baseline.decode('utf-8-sig').replace('\r\n', '\n')
old = 'int runRuntimeCommandSmokeTest(const RuntimeConfig& config) {'
new = '''#include "RuntimePlayerCommandSmoke.inl"

int runRuntimeCommandSmokeTest(const RuntimeConfig& config) {
    std::string playerCommandsError;
    if (!runRuntimePlayerCommandSmokeProbe(playerCommandsError)) {
        std::cerr << "Command smoke failed: " << playerCommandsError << "\\n";
        return 1;
    }
    std::cout << "Player commands: real waypoint movement/Stop/Follow/factory fallback/rally exit passed\\n";
    if (std::getenv("CONCORD_COMMAND_SMOKE_PLAYER_COMMANDS_ONLY") != nullptr) return 0;'''
if source.count(old) != 1:
    raise SystemExit(f'player smoke anchor mismatch: {source.count(old)}')
source = source.replace(old, new, 1)
print(f'Prepared player runtime smoke for {path}')
if '--apply' in sys.argv:
    if path.read_bytes() != baseline:
        raise SystemExit('RuntimeShell changed during integration; retry')
    output = source.replace('\n', newline).encode('utf-8')
    path.write_bytes((b'\xef\xbb\xbf' if bom else b'') + output)
    print('Applied player runtime smoke.')
else:
    print('Dry run only; pass --apply to write.')
