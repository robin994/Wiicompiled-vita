"""Read the packaged Vita metadata rather than inferring it from build flags."""
import struct


def read_sfo(data):
    if len(data) < 20:
        raise ValueError('Truncated SFO header')
    magic, version, keys, values, count = struct.unpack_from('<5I', data)
    if magic != 0x46535000 or version != 0x101:
        raise ValueError('Unsupported SFO header')
    if 20 + count * 16 > keys or keys > values or values > len(data):
        raise ValueError('Invalid SFO table offsets')
    result = {}
    for index in range(count):
        key_offset, fmt, length, maximum, value_offset = struct.unpack_from('<HHIII', data, 20 + index * 16)
        start = keys + key_offset
        end = data.find(b'\0', start, values)
        if start >= values or end < 0:
            raise ValueError('Invalid SFO key')
        name = data[start:end].decode('utf-8')
        offset = values + value_offset
        if length > maximum or offset + maximum > len(data):
            raise ValueError(f'Invalid SFO value: {name}')
        value = data[offset:offset + length]
        if fmt == 0x404 and length == 4:
            value = struct.unpack('<I', value)[0]
        elif fmt == 0x204:
            value = value.rstrip(b'\0').decode('utf-8')
        else:
            continue
        result[name] = value
    return result


def verify_package_sfo(data, expected_title_id):
    fields = read_sfo(data)
    if fields.get('ATTRIBUTE2') != 12:
        raise ValueError(f'Packaged ATTRIBUTE2 must be 12, got {fields.get("ATTRIBUTE2")}')
    if fields.get('TITLE_ID') != expected_title_id:
        raise ValueError(f'Packaged TITLE_ID must be {expected_title_id}, got {fields.get("TITLE_ID")}')
    return fields
