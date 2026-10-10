"""Transport Aurora's native GXM cache records without recompiling shaders."""
import hashlib
import os
import re
import struct
from pathlib import Path

MAX_BYTES = 4 * 1024 * 1024
NAME = re.compile(r'([vf])-([0-9a-f]{16})\.gxp\Z')

def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value

def validate(path):
    match = NAME.fullmatch(path.name)
    if not match or path.is_symlink() or not path.is_file():
        raise ValueError('invalid shader cache name or file: ' + str(path))
    if not 48 <= path.stat().st_size <= MAX_BYTES + 32:
        raise ValueError('invalid shader cache extent: ' + str(path))
    data = path.read_bytes()
    magic, version, source, checksum, length, stage = struct.unpack_from('<IIQQII', data)
    payload = data[32:]
    expected_stage = 1 if match[1] == 'v' else 2
    if (magic != 0x41564758 or version != 1 or source != int(match[2], 16) or
            stage != expected_stage or not 16 <= length <= MAX_BYTES or
            length != len(payload) or checksum != fnv(payload)):
        raise ValueError('shader cache identity/checksum mismatch: ' + str(path))
    return data

def import_cache(source, payload_root):
    source, payload_root = Path(source), Path(payload_root)
    # Validate the complete input set before publishing any file. Device GXM
    # still checks executable GXP structure before registering a program.
    records = [(path, validate(path)) for path in sorted(source.glob('*.gxp'))]
    if not records:
        raise ValueError('no GXM cache records in ' + str(source))
    destination = payload_root / 'native/v1/shaders/gxm-cg-gxp-v1'
    for path, data in records:
        existing = destination / path.name
        if existing.exists() and existing.read_bytes() != data:
            raise ValueError('shader identity collision: ' + str(existing))
    destination.mkdir(parents=True, exist_ok=True)
    result = []
    for path, data in records:
        output = destination / path.name
        if not output.exists():
            temporary = output.with_suffix('.gxp.tmp')
            temporary.write_bytes(data)
            os.replace(temporary, output)
        result.append({'path': str(output.relative_to(payload_root)), 'bytes': len(data),
                       'sha256': hashlib.sha256(data).hexdigest()})
    return {'abi': 'aurora-gxm-cg-gxp-v1', 'records': result,
            'scope': 'cache header, source/stage identity, extent and payload checksum; '
                     'GXP executable validation remains on Vita'}
