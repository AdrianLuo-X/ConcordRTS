"""Respect configured bindings before fallback quick save/load shortcuts."""
from pathlib import Path
import sys

path = Path(__file__).resolve().parents[1] / 'src' / 'RuntimeShell.cpp'
baseline = path.read_bytes()
newline = '\r\n' if b'\r\n' in baseline else '\n'
bom = baseline.startswith(b'\xef\xbb\xbf')
source = baseline.decode('utf-8-sig').replace('\r\n', '\n')
old = '''        if ((controlDown && (wParam == 'S' || wParam == 'L'))
            || wParam == VK_F5
            || wParam == VK_F9) {'''
new = '''        const int actualBindingCode = runtimePlayerCurrentKeyCode(wParam);
        const bool configuredGameplayBinding = std::any_of(
            state->runtimeMenuKeyboardBindings.begin(), state->runtimeMenuKeyboardBindings.end(),
            [&](const auto& binding) {
                return binding.first != "Options" && binding.second == actualBindingCode;
            });
        if (!configuredGameplayBinding
            && ((controlDown && (wParam == 'S' || wParam == 'L'))
                || wParam == VK_F5 || wParam == VK_F9)) {'''
if source.count(old) != 1:
    raise SystemExit(f'shortcut priority anchor mismatch: {source.count(old)}')
source = source.replace(old, new, 1)
print(f'Prepared shortcut priority for {path}')
if '--apply' in sys.argv:
    if path.read_bytes() != baseline:
        raise SystemExit('RuntimeShell changed during integration; retry')
    output = source.replace('\n', newline).encode('utf-8')
    path.write_bytes((b'\xef\xbb\xbf' if bom else b'') + output)
    print('Applied shortcut priority.')
else:
    print('Dry run only; pass --apply to write.')
