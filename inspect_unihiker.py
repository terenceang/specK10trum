import os
import json
from pathlib import Path
home = Path(os.environ['USERPROFILE']) / '.platformio'
pkgs = home / 'packages'
found = False
for p in pkgs.rglob('boards/*.json'):
    try:
        data = json.loads(p.read_text(encoding='utf-8'))
    except Exception:
        continue
    if 'unihiker' in p.name.lower() or 'unihiker' in str(p.parent).lower() or 'unihiker_k10' in data.get('id', '') or 'unihiker' in data.get('name', '').lower():
        found = True
        print('FILE', p)
        print('ID', data.get('id'))
        print('NAME', data.get('name'))
        if 'platform' in data:
            print('PLATFORM', data['platform'])
        if 'build' in data:
            print('BUILD', data['build'])
        if 'extra' in data:
            print('EXTRA', data['extra'])
        print('---')
if not found:
    print('NO UNIHIKER')
