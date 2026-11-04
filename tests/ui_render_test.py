"""Check unchanged activity framebuffers against the pre-refactor snapshots."""
import hashlib
import json
from pathlib import Path

root=Path(__file__).resolve().parents[1]
names=json.loads((root/'build/ui-preview/screens.json').read_text())
frames=(root/'build/ui-preview.bin').read_bytes()
expected=json.loads((root/'tests/ui_snapshots.json').read_text())
for name,digest in expected.items():
    index=names.index(name)
    actual=hashlib.sha256(frames[index*1024:(index+1)*1024]).hexdigest()
    assert actual==digest, f'{name}: activity framebuffer changed; inspect before updating snapshot'
print(f'{len(expected)} activity framebuffers match pre-refactor snapshots byte for byte.')
