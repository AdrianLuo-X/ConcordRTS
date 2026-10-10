"""Follow-up shell wiring after integrate_player_commands.py, default dry-run."""
from pathlib import Path
import sys

path = Path(__file__).resolve().parents[3] / 'src' / 'runtime' / 'host' / 'RuntimeShell.cpp'
baseline = path.read_bytes()
newline = '\r\n' if b'\r\n' in baseline else '\n'
bom = baseline.startswith(b'\xef\xbb\xbf')
source = baseline.decode('utf-8-sig').replace('\r\n', '\n')
def once(old, new):
    global source
    if source.count(old) != 1:
        raise RuntimeError(f'expected one ({source.count(old)}): {old[:100]}')
    source = source.replace(old, new, 1)

once('''            constexpr const char* preferenceBindingIds[] = {
                "Options",
                "StopObject",
                "GuardObject",
                "ScatterObject",
                "DeployObject",
                "CenterView"
            };
            config.keyBindings.clear();
            for (const char* bindingId : preferenceBindingIds) {''', '''            std::vector<std::string> preferenceBindingIds = {
                "Options", "StopObject", "GuardObject", "ScatterObject", "DeployObject", "CenterView"
            };
            for (const auto& binding : runtimePlayerCommandBindings())
                preferenceBindingIds.push_back(binding.first);
            config.keyBindings.clear();
            for (const std::string& bindingId : preferenceBindingIds) {''')
once('''                if (keyCode > 0 && keyCode <= 255) {
                    config.keyBindings.emplace_back(bindingId, keyCode);''',
    '''                if (runtimePlayerValidKeyCode(keyCode)) {
                    config.keyBindings.emplace_back(bindingId, keyCode);''')
once('        state.triggers.campaignRadarEvents.push_back(event);',
    '''        state.triggers.campaignRadarEvents.push_back(event);
        state.playerLastRadarCell = RuntimeMultiplayerGameplayCell{event.cell.x, event.cell.y};''')
source = source.replace('state.playerLastRadarCell = {candidate.x, candidate.y};',
    'state.playerLastRadarCell = RuntimeMultiplayerGameplayCell{candidate.x, candidate.y};')
once('''    if (state.multiplayerBridge == nullptr) {
        return executeRuntimeSecondaryAction(state, objectId);''',
    '''    if (state.multiplayerBridge == nullptr) {
        state.playerCommands.cancel({objectId});
        return executeRuntimeSecondaryAction(state, objectId);''')
once('''    if (state.multiplayerBridge == nullptr) {
        return executeRuntimeSecondaryActionForSelection(state);''',
    '''    if (state.multiplayerBridge == nullptr) {
        state.playerCommands.cancel(state.selectedObjectIds);
        return executeRuntimeSecondaryActionForSelection(state);''')

# Existing runtime smoke exercises persistence; add an actual modifier binding
# to the configuration probe rather than only testing bit arithmetic.
print(f'Prepared delta for {path}: {len(source)} characters')
if '--apply' in sys.argv:
    if path.read_bytes() != baseline:
        raise SystemExit('RuntimeShell changed during integration; retry')
    output = source.replace('\n', newline).encode('utf-8')
    path.write_bytes((b'\xef\xbb\xbf' if bom else b'') + output)
    print('Applied player command delta.')
else:
    print('Dry run only; pass --apply to write.')
