"""Wire primary-airport preference into derived matching input; default dry-run."""
from pathlib import Path
import sys

path = Path(__file__).resolve().parents[1] / 'src' / 'RuntimeShell.cpp'
baseline = path.read_bytes()
newline = '\r\n' if b'\r\n' in baseline else '\n'
bom = baseline.startswith(b'\xef\xbb\xbf')
source = baseline.decode('utf-8-sig').replace('\r\n', '\n')
old = '''            slot.poweredOperational = !building->powered
                || host.poweredOperational;
            slotIndexByKey.emplace('''
new = '''            slot.poweredOperational = !building->powered
                || host.poweredOperational;
            const std::string primaryCategory = toUpperAsciiLocal(building->factory)
                + ((building->naval || building->waterBound) ? ":NAVAL" : ":LAND");
            const auto primary = state.playerCommands.primaryFactories.find({
                runtimeCanonicalHumanHouseOwner(owner), primaryCategory});
            slot.preferredPrimary = slot.poweredOperational
                && primary != state.playerCommands.primaryFactories.end()
                && primary->second == host.instanceId;
            slotIndexByKey.emplace('''
if source.count(old) != 1:
    raise SystemExit(f'primary airport anchor mismatch: {source.count(old)}')
source = source.replace(old, new, 1)
print(f'Prepared primary airport wiring for {path}')
if '--apply' in sys.argv:
    if path.read_bytes() != baseline:
        raise SystemExit('RuntimeShell changed during integration; retry')
    output = source.replace('\n', newline).encode('utf-8')
    path.write_bytes((b'\xef\xbb\xbf' if bom else b'') + output)
    print('Applied primary airport wiring.')
else:
    print('Dry run only; pass --apply to write.')
