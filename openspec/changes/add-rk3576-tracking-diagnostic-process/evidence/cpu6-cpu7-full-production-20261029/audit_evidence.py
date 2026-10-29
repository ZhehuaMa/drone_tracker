#!/usr/bin/env python3
"""Verify the original transferred files without treating later analyses as originals."""
import argparse
import hashlib
import json
from pathlib import Path


def fingerprint(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return {'sha256': digest.hexdigest(), 'bytes': path.stat().st_size}


def audit(root):
    result = {'runs': [], 'checked_original_files': 0, 'mismatches': []}
    for number, cpu in enumerate((6, 7, 7, 6, 7, 6, 6, 7), 1):
        run_id = f'batch02-run{number:02d}-cpu{cpu}'
        directory = root / run_id
        raw = directory / 'raw' / run_id
        manifest = json.loads((directory / 'remote-manifest.json').read_text())
        bad = [name for name, expected in manifest.items()
               if not (raw / name).is_file() or fingerprint(raw / name) != expected]
        extras = sorted(str(path.relative_to(raw)) for path in raw.rglob('*')
                        if path.is_file() and str(path.relative_to(raw)) not in manifest)
        result['runs'].append({'run_id': run_id, 'checked_original_files': len(manifest),
                               'mismatches': bad, 'additional_host_derived_files': extras})
        result['checked_original_files'] += len(manifest)
        result['mismatches'].extend(f'{run_id}/{name}' for name in bad)
    controllers = json.loads((root / 'controller-v2-manifest.json').read_text())
    result['controller_mismatches'] = [name for name, expected in controllers.items()
                                       if fingerprint(root / name) != expected]
    result['passed'] = not result['mismatches'] and not result['controller_mismatches']
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.root)
    with args.output.open('x') as destination:
        json.dump(result, destination, indent=2)
        destination.write('\n')
    print(json.dumps({key: value for key, value in result.items() if key != 'runs'}))
    raise SystemExit(0 if result['passed'] else 1)
