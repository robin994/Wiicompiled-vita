#!/usr/bin/env python3
import struct
import tempfile
from pathlib import Path
from native_assets.shader_cache import fnv, import_cache, validate

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    source = root / 'cache'; source.mkdir()
    payload = root / 'payload'
    data = bytes(range(16))
    good = source / 'f-000000000000002a.gxp'
    good.write_bytes(struct.pack('<IIQQII', 0x41564758, 1, 42, fnv(data), len(data), 2) + data)
    before = good.read_bytes()
    first = import_cache(source, payload)
    assert import_cache(source, payload) == first
    assert (payload / first['records'][0]['path']).read_bytes() == before
    for offset in [0, 4, 8, 16, 24, 28, 35]:
        bad = bytearray(before); bad[offset] ^= 1; good.write_bytes(bad)
        try:
            validate(good)
        except ValueError:
            pass
        else:
            raise AssertionError('corrupt shader accepted at ' + str(offset))
    good.write_bytes(before)
    invalid = source / 'v-000000000000002b.gxp'; invalid.write_bytes(before)
    untouched = root / 'untouched'
    try:
        import_cache(source, untouched)
    except ValueError:
        pass
    else:
        raise AssertionError('stage/source mismatch accepted')
    assert not untouched.exists(), 'partial shader set was published'
print('PASS: GXM cache transport, idempotence and corruption rejection')
