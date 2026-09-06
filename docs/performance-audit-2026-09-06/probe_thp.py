#!/usr/bin/env python3
"""Read-only host diagnostic. No game assets are written or modified.

Reproduce the native override's normal JPEG decode on THP video components,
then test byte stuffing of the bounded entropy payload on sampled disc frames.
This is an audit probe, not a general THP decoder or an implementation patch.
"""
import argparse
import ctypes as C
import hashlib
import json
import struct
from pathlib import Path


def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def frames(data):
    assert data[:4] == b"THP\0"
    comp_offset = u32(data, 32)
    count = u32(data, comp_offset)
    types = data[comp_offset + 4:comp_offset + 4 + count]
    assert types[0] == 0 and count in (1, 2)
    offset, size = u32(data, 40), u32(data, 24)
    result = []
    for number in range(u32(data, 20)):
        assert offset + size <= len(data)
        video_size = u32(data, offset + 8)
        start = offset + 8 + count * 4
        assert start + video_size <= offset + size
        result.append((number, start, video_size))
        offset, size = offset + size, u32(data, offset)
    return result


def stuff_thp(jpeg):
    # All audited samples have one baseline sequential scan and no DRI.
    assert jpeg[:2] == b"\xff\xd8"
    offset = 2
    markers = []
    while offset < len(jpeg):
        assert jpeg[offset] == 255
        marker = jpeg[offset + 1]
        markers.append(marker)
        assert marker not in (0xDD, 0xC2), "Probe excludes restart/progressive scans"
        offset += 2 + struct.unpack_from(">H", jpeg, offset + 2)[0]
        if marker == 0xDA:
            break
    assert markers[-1] == 0xDA
    # Container supplies the exact video component boundary; no RAM scan.
    eoi = jpeg.rfind(b"\xff\xd9")
    assert eoi >= offset and not any(jpeg[eoi + 2:])
    entropy = jpeg[offset:eoi]
    return jpeg[:offset] + entropy.replace(b"\xff", b"\xff\0") + b"\xff\xd9", {
        "entropy_offset": offset,
        "entropy_ff_count": entropy.count(b"\xff"),
        "first_eoi_offset": jpeg.find(b"\xff\xd9"),
        "actual_eoi_offset": eoi,
    }


class Decoder:
    def __init__(self, lib_path):
        self.lib = lib = C.CDLL(lib_path)
        lib.tjInitDecompress.restype = C.c_void_p
        lib.tjDestroy.argtypes = [C.c_void_p]
        lib.tjDecompressHeader3.argtypes = [C.c_void_p, C.c_void_p, C.c_ulong] + [C.POINTER(C.c_int)] * 4
        lib.tjPlaneWidth.argtypes = [C.c_int] * 3
        lib.tjPlaneHeight.argtypes = [C.c_int] * 3
        lib.tjDecompressToYUVPlanes.argtypes = [C.c_void_p, C.c_void_p, C.c_ulong, C.POINTER(C.c_void_p), C.c_int, C.POINTER(C.c_int), C.c_int, C.c_int]
        lib.tjGetErrorStr2.argtypes = [C.c_void_p]
        lib.tjGetErrorStr2.restype = C.c_char_p
        lib.tjGetErrorCode.argtypes = [C.c_void_p]

    def run(self, source):
        lib = self.lib
        handle = lib.tjInitDecompress()
        width, height, subsamp, colorspace = [C.c_int() for _ in range(4)]
        src = C.create_string_buffer(source)
        try:
            header = lib.tjDecompressHeader3(handle, src, len(source), *[C.byref(x) for x in (width, height, subsamp, colorspace)])
            result = {"header_result": header, "width": width.value, "height": height.value, "subsamp": subsamp.value}
            if header != 0:
                result["error"] = lib.tjGetErrorStr2(handle).decode()
                return result
            strides = (C.c_int * 3)(*[lib.tjPlaneWidth(c, width.value, subsamp.value) for c in range(3)])
            sizes = [strides[c] * lib.tjPlaneHeight(c, height.value, subsamp.value) for c in range(3)]
            buffers = [(C.c_ubyte * size)() for size in sizes]
            pointers = (C.c_void_p * 3)(*[C.addressof(buf) for buf in buffers])
            status = lib.tjDecompressToYUVPlanes(handle, src, len(source), pointers, width.value, strides, height.value, 2048)
            result["pixels_result"] = status
            if status != 0:
                result["error"] = lib.tjGetErrorStr2(handle).decode()
                result["error_code"] = lib.tjGetErrorCode(handle)
            else:
                result["yuv_sha256"] = hashlib.sha256(b"".join(bytes(buf) for buf in buffers)).hexdigest()
            return result
        finally:
            lib.tjDestroy(handle)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--assets", default="local/mkwii-disc/DATA/files/thp")
    parser.add_argument("--library", default="/opt/homebrew/lib/libturbojpeg.dylib")
    parser.add_argument("--output", default="docs/performance-audit-2026-09-06/thp-probe.json")
    args = parser.parse_args()
    decoder = Decoder(args.library)
    results = []
    for path in sorted(Path(args.assets).rglob("*.thp")):
        data = path.read_bytes()
        index = frames(data)
        for sample in sorted({0, len(index) // 2, len(index) - 1}):
            number, offset, size = index[sample]
            source = data[offset:offset + size]
            corrected, info = stuff_thp(source)
            current_size = source.find(b"\xff\xd9", 2) + 2
            original = decoder.run(source[:current_size])
            bounded = decoder.run(source)
            stuffed = decoder.run(corrected)
            results.append({"path": str(path), "frame": number, "offset": offset, "source_size": size, "source_sha256": hashlib.sha256(source).hexdigest(), **info, "current_scanned": original, "bounded_original": bounded, "stuffed": stuffed})
    summary = {
        "sample_count": len(results),
        "asset_count": len({row["path"] for row in results}),
        "current_decode_failures": sum(row["current_scanned"].get("pixels_result") != 0 for row in results),
        "bounded_original_decode_failures": sum(row["bounded_original"].get("pixels_result") != 0 for row in results),
        "stuffed_decode_failures": sum(row["stuffed"].get("pixels_result") != 0 for row in results),
        "premature_eoi_scans": sum(row["first_eoi_offset"] != row["actual_eoi_offset"] for row in results),
    }
    output = {"scope": "Host functional reproduction only. Not Vita timing or runtime validation.", "libturbojpeg": str(Path(args.library).resolve()), "summary": summary, "samples": results}
    Path(args.output).write_text(json.dumps(output, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
