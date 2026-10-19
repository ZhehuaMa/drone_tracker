"""Versioned immutable records, including read-only historical reference contracts."""
import hashlib
import json
import math
from pathlib import Path

CASE_ID = 'wlcb-1513-loss-recovery'
VERSION = 1
START_US, END_US = 55_000_000, 75_000_000
SOURCE = 'test-01-20260929-1513/session-ac89d6c15b83faf1c92abed84a07ab02'
SOURCE_HASHES = {
    'test-01-20260929-1513.mp4': '11d45885dc32859c5aba16541ecc83f769ad47ba9ae66b54908b19d805d4c152',
    '000001.frames.jsonl': '3faf3d09ede981fb8792679eca2c5113c162abee59def4458e51aae066e03ae0',
    '000002.frames.jsonl': '938f931b2ef4c3debb3f820a86634356deaf02fc0154bbcce5f316bc37f265e0',
}

def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()

def load(path):
    return json.loads(Path(path).read_text())

def lines(path):
    return [json.loads(row) for row in Path(path).read_text().splitlines() if row.strip()]

def write(path, value):
    with Path(path).open('x') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write('\n')

def write_lines(path, rows):
    with Path(path).open('x') as stream:
        for row in rows:
            stream.write(json.dumps(row, ensure_ascii=False, allow_nan=False) + '\n')

def verify_hash(path, expected):
    actual = sha256(path)
    if actual != expected:
        raise ValueError(f'{path}: sha256 mismatch: expected {expected}, actual {actual}')

def validate(value, schema, field='$'):
    """Validate the documented JSON Schema subset without a host dependency."""
    kinds = {'object': dict, 'array': list, 'string': str, 'integer': int,
             'number': (int, float), 'boolean': bool, 'null': type(None)}
    if isinstance(value, float) and not math.isfinite(value):
        raise ValueError(f'{field}: non-finite number')
    kind = schema.get('type')
    if kind and (not isinstance(value, kinds[kind]) or
                 (kind in ('integer', 'number') and isinstance(value, bool))):
        raise ValueError(f'{field}: expected {kind}')
    if 'const' in schema and value != schema['const']:
        raise ValueError(f'{field}: expected {schema["const"]!r}')
    if 'enum' in schema and value not in schema['enum']:
        raise ValueError(f'{field}: unsupported value {value!r}')
    if isinstance(value, dict):
        for required in schema.get('required', []):
            if required not in value:
                raise ValueError(f'{field}.{required}: missing required field')
        if schema.get('additionalProperties') is False:
            for key in value:
                if key not in schema.get('properties', {}):
                    raise ValueError(f'{field}.{key}: unsupported field')
        for key, child in schema.get('properties', {}).items():
            if key in value:
                validate(value[key], child, f'{field}.{key}')
    if isinstance(value, list):
        if len(value) < schema.get('minItems', 0):
            raise ValueError(f'{field}: too few items')
        for index, child in enumerate(value):
            validate(child, schema.get('items', {}), f'{field}[{index}]')
    if isinstance(value, (int, float)) and value < schema.get('minimum', float('-inf')):
        raise ValueError(f'{field}: below minimum')
    if isinstance(value, str) and len(value) < schema.get('minLength', 0):
        raise ValueError(f'{field}: empty string')

def validate_record(value, name):
    version = value.get('schema_version') if isinstance(value, dict) else None
    current = 3 if name == 'run_config' else 2
    versioned = {'fixture', 'preparation', 'identity', 'completion', 'baseline', 'comparison', 'run_config', 'metrics'}
    suffix = f'.v{current}' if name in versioned and version == current else ''
    validate(value, load(Path(__file__).parent / 'schema' / (name + suffix + '.schema.json')))
