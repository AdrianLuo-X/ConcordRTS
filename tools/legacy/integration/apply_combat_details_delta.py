from pathlib import Path
import json
import sys

root = Path(__file__).resolve().parents[3]
target = root / 'src' / 'runtime' / 'host' / 'RuntimeShell.cpp'
data = target.read_bytes()
had_bom = data.startswith(b'\xef\xbb\xbf')
newline = '\r\n' if b'\r\n' in data else '\n'
text = data.decode('utf-8-sig').replace('\r\n', '\n')
edits = json.loads((root / 'docs' / 'history' / 'integration' / 'combat_details_delta.json').read_text(encoding='utf-8'))
for edit in edits:
    count = text.count(edit['old'])
    expected = edit.get('count', 1)
    if count != expected:
        raise SystemExit(f"{edit['name']}: expected {expected} matches, found {count}; no files written")
    text = text.replace(edit['old'], edit['new'])
if '--check' in sys.argv:
    print(f'{len(edits)} exact combat delta edits verified; no files written')
else:
    if target.read_bytes() != data:
        raise SystemExit('RuntimeShell changed during integration; no files written')
    encoded = text.replace('\n', newline).encode('utf-8')
    target.write_bytes((b'\xef\xbb\xbf' if had_bom else b'') + encoded)
    print(f'{len(edits)} combat delta edits applied')
