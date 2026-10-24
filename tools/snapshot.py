# Archive de retour arriere du projet (hors .pio/ et .git/) avec
# manifeste SHA-256, plus les artefacts firmware compiles si presents.
# Creation : python tools/snapshot.py <racine_du_projet>
# Controle : python tools/snapshot.py <racine> --verify <archive.zip>
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
parser.add_argument('--verify', type=Path)
args = parser.parse_args()
root = args.root.resolve()
if args.verify:
    with zipfile.ZipFile(args.verify) as archive:
        assert archive.testzip() is None, 'Archive CRC failure'
        manifest = json.loads(archive.read('_reference_manifest.json'))
        changed = []
        for item in manifest['files']:
            data = archive.read(item['path'])
            assert hashlib.sha256(data).hexdigest() == item['sha256'], item['path']
            current = root / item['path']
            if not current.is_file() or hashlib.sha256(current.read_bytes()).hexdigest() != item['sha256']:
                changed.append(item['path'])
        print(json.dumps({'archive_verified': True, 'archived_files': len(manifest['files']),
                          'changed_since_snapshot': changed}, ensure_ascii=False, indent=2))
else:
    paths = []
    for item in sorted(root.iterdir()):
        if item.name in ('.pio', '.git', 'logs'):
            continue
        paths.extend(sorted(item.rglob('*')) if item.is_dir() else [item])
    paths = [p for p in paths if p.is_file()]
    for name in ('firmware.bin', 'firmware.elf', 'firmware.map'):
        item = root / '.pio' / 'build' / 'blackpill_f411ce' / name
        if item.is_file():
            paths.append(item)
    destination = root.parent / ('Projet_ventillateur_reference_' + datetime.now().strftime('%Y%m%d_%H%M%S') + '.zip')
    manifest = {'created_at': datetime.now().astimezone().isoformat(), 'root': str(root),
                'purpose': 'Reference before refactoring, no device access', 'files': []}
    with zipfile.ZipFile(destination, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for item in paths:
            data = item.read_bytes()
            relative = item.relative_to(root).as_posix()
            archive.writestr(relative, data)
            manifest['files'].append({'path': relative, 'bytes': len(data),
                                      'sha256': hashlib.sha256(data).hexdigest()})
        archive.writestr('_reference_manifest.json', json.dumps(manifest, ensure_ascii=False, indent=2))
    with zipfile.ZipFile(destination) as archive:
        assert archive.testzip() is None
        for item in manifest['files']:
            assert hashlib.sha256(archive.read(item['path'])).hexdigest() == item['sha256']
    print(json.dumps({'archive': str(destination), 'files': len(paths), 'bytes': destination.stat().st_size,
                      'sha256': hashlib.sha256(destination.read_bytes()).hexdigest(), 'verified': True}, indent=2))
