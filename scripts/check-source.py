#!/usr/bin/env python3
"""Bounded source/config checks, NOT the Harmony SDK schema or compiler."""
import json
from pathlib import Path
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
count = 0
for file in sorted(root.rglob('*.json')) + sorted(root.rglob('*.json5')):
    if any(p in ('build', '.hvigor', 'oh_modules', '.git') for p in file.relative_to(root).parts):
        continue
    text = file.read_text(encoding='utf-8')
    # These checked-in JSON5 files only extend JSON with full-line // comments.
    if file.suffix == '.json5':
        text = '\n'.join(line for line in text.splitlines() if not line.lstrip().startswith('//'))
    json.loads(text)
    count += 1
for svg in root.rglob('*.svg'):
    if not any(p in ('build', '.hvigor', 'oh_modules', '.git') for p in svg.relative_to(root).parts):
        ET.parse(svg)
module = json.loads((root / 'entry/src/main/module.json5').read_text())['module']
assert module['requestPermissions'] == [{'name': 'ohos.permission.INTERNET'}]
assert json.loads((root / 'oh-package.json5').read_text())['license'] == 'MIT'
for name in ('LICENSE-project.txt', 'LICENSE-pyatv.txt', 'LICENSE-OpenSSL-3.5.9.txt', 'Apache-2.0.txt'):
    assert (root / 'licenses' / name).read_bytes() == (root / 'entry/src/main/resources/rawfile/licenses' / name).read_bytes()
for name in ('crypto', 'codec', 'session'):
    assert (root / f'native/src/{name}.cpp').is_file()
    assert (root / f'native/include/companion/{name}.hpp').is_file()
assert (root / 'native/bridge/napi.cpp').is_file()
for file in root.rglob('*'):
    if not file.is_file() or any(p in ('build', '.hvigor', 'oh_modules', '.git') for p in file.relative_to(root).parts):
        continue
    content = file.read_text(encoding='utf-8')
    assert '\r' not in content, f'Unexpected CR: {file}'
    assert all(line == line.rstrip() for line in content.splitlines()), f'Trailing whitespace: {file}'
print(f'PASS source checks: {count} JSON/JSON5 files, SVG XML, source paths, INTERNET-only permissions, MIT metadata, exact bundled licenses, whitespace.')
print('NOT Harmony SDK schema validation, ArkTS compilation, CMake execution, or HAP packaging.')
