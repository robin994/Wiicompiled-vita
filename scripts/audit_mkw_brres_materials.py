#!/usr/bin/env python3
"""Read-only Mario Kart Wii PAL BRRES material inventory, compared with a Vita3K draw CSV.

This tool intentionally never exports Nintendo geometry, pixels or animation
tracks. It emits only descriptive metadata, TEV bitfields and SHA-256 hashes.

BRRES resource layout follows the public NW4R format as interpreted by the
existing scripts/native_assets/formats.py. The material offsets were
cross-checked against noclip.website (MIT, pinned by MKW-3DS); all code here
is original. GX BP bit positions are those used by Aurora-Vita's
lib/gx/command_processor.cpp. Do not import code from the original MKW-3DS
port (its baseline is distributed without an explicit license).

Run: python3 scripts/audit_mkw_brres_materials.py --output build/material-audit.json
"""
from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

from vita_psarc import iter_entry, parse_psarc
from native_assets.formats import (GX_FORMAT,cstring,dictionary,extent,leaves,
                                   mip_size,sections,textures,u16,u32)

DEFAULT_ASSETS = ("disc/Race/Kart/ma_kart-mr.szs",
                  "disc/Race/Kart/mdf_kart-mr.szs")
MAX_ARCHIVE_ENTRY = 16 * 1024 * 1024
MAX_MATERIALS = 128
MAX_CLIPS = 128
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
SOURCE_HASH_LIMIT = 1024 * 1024


def source_fnv64(pixels: bytes) -> str:
    value=FNV_OFFSET
    for byte in pixels[:SOURCE_HASH_LIMIT]:
        value=((value^byte)*FNV_PRIME)&0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


def source_fingerprints(pixels: bytes,width: int,height: int,gx_format: int,
                        mip_count: int) -> list[dict]:
    """Hashes the *encoded* GX bytes, never decoded or GXM-uploaded pixels.

    Multiple mip-prefixes may be bound by GX. Hash successive segments only
    once, mirroring the Vita one-megabyte cap and FNV64 implementation.
    """
    prefixes=[mip_size(width,height,gx_format,n) for n in range(1,mip_count+1)]
    if not prefixes or prefixes[-1]>len(pixels):
        raise ValueError("mip prefix exceeds TEX0 data")
    hash_value=FNV_OFFSET
    prev=0
    output=[]
    for level,size in enumerate(prefixes,1):
        count=min(size,SOURCE_HASH_LIMIT)
        for v in pixels[prev:count]:
            hash_value=((hash_value ^ v)*FNV_PRIME)&0xFFFFFFFFFFFFFFFF
        prev=count
        output.append({"mip_count":level,"source_bytes":size,
                       "source_hash_bytes":count,
                       "source_fnv64":f"{hash_value:016x}"})
    return output


def signed_byte(byte: int) -> int:
    return byte - 256 if byte & 0x80 else byte


def bounds(data: bytes, offset: int, size: int) -> None:
    extent(data, offset, size)


def gx_bp_commands(data: bytes) -> dict[int, int]:
    """Parse a bounded Wii FIFO display list. 0 is padding, not a command."""
    if len(data) > 4096:
        raise ValueError("unexpected material display-list length")
    cursor, registers = 0, {}
    while cursor < len(data):
        opcode = data[cursor]
        cursor += 1
        if opcode == 0:
            continue
        if opcode == 0x61:  # one 24-bit BP register write
            bounds(data, cursor, 4)
            word = u32(data, cursor)
            registers[word >> 24] = word & 0xFFFFFF
            cursor += 4
        elif opcode == 0x10:  # count+address, then payload words: XF
            bounds(data, cursor, 4)
            header = u32(data, cursor)
            cursor += 4
            count = (header >> 16) + 1
            bounds(data, cursor, count * 4)
            cursor += count * 4
        elif opcode == 0x08:  # CP register
            bounds(data, cursor, 5)
            cursor += 5
        else:
            raise ValueError(f"unsupported material GX command {opcode:#04x}")
    return registers


def tev_stage(bp: dict[int, int], index: int) -> dict:
    tref = bp.get(0x28 + index // 2)
    color = bp.get(0xC0 + 2 * index)
    alpha = bp.get(0xC1 + 2 * index)
    if any(x is None for x in (tref, color, alpha)):
        raise ValueError(f"TEV stage {index} has incomplete BP state")
    shift = (index & 1) * 12
    map_id = (tref >> shift) & 7 if (tref >> (shift + 6)) & 1 else None
    coord = (tref >> (shift + 3)) & 7
    color_in = [(color >> v) & 15 for v in (12, 8, 4, 0)]
    alpha_in = [(alpha >> v) & 7 for v in (13, 10, 7, 4)]
    return {
        "index": index,
        "texmap": map_id,
        "texcoord": coord,
        "raster_channel_hw": (tref >> (shift + 7)) & 7,
        "color_args_abcd": color_in,
        "alpha_args_abcd": alpha_in,
        "color_mode": {
            "subtract_or_compare": (color >> 18) & 1,
            "bias": (color >> 16) & 3,
            "scale": (color >> 20) & 3,
            "clamp": bool((color >> 19) & 1),
            "register": (color >> 22) & 3,
        },
        "alpha_mode": {
            "subtract_or_compare": (alpha >> 18) & 1,
            "bias": (alpha >> 16) & 3,
            "scale": (alpha >> 20) & 3,
            "clamp": bool((alpha >> 19) & 1),
            "register": (alpha >> 22) & 3,
        },
        "raw_bp_color": f"{color:06x}",
        "raw_bp_alpha": f"{alpha:06x}",
    }


def material_entry(data: bytes, base: int, name: str, revision: int) -> dict:
    size = u32(data, base)
    if not 0x560 <= size <= 64 * 1024:
        raise ValueError(f"unsupported material size: {name} {size}")
    bounds(data, base, size)
    stage_count = data[base + 0x16]
    texture_gen_count = data[base + 0x14]
    channel_count = data[base + 0x15]
    ind_count = data[base + 0x17]
    if not 1 <= stage_count <= 16 or texture_gen_count > 8 or channel_count > 2 or ind_count > 4:
        raise ValueError(f"invalid GX generator/stage counts: {name}")
    tev_rel = u32(data, base + 0x28)
    num_samplers = u32(data, base + 0x2C)
    sampler_rel = u32(data, base + 0x30)
    if num_samplers > 8 or sampler_rel + num_samplers * 0x34 > size:
        raise ValueError(f"invalid material samplers: {name}")
    # MDL0 >= v10 adds a Fur field before user-data and material DL.
    mat_dl_field = 0x3C if revision >= 10 else 0x38
    dl_rel = u32(data, base + mat_dl_field)
    if dl_rel + 384 > size or tev_rel == 0:
        raise ValueError(f"invalid material TEV/display-list pointer: {name}")
    tev_base = base + tev_rel
    bounds(data, tev_base, 512)
    if u32(data, tev_base) != 512 or u32(data, tev_base + 8) >= 32:
        raise ValueError(f"unexpected material TEV shader: {name}")
    mat_bp = gx_bp_commands(extent(data, base + dl_rel, 384))
    tev_bp = gx_bp_commands(extent(data, tev_base + 32, 480))
    bp = {**mat_bp, **tev_bp}
    samplers: list[dict] = []
    for i in range(num_samplers):
        pos = base + sampler_rel + i * 0x34
        name_rel, palette_rel = u32(data, pos), u32(data, pos + 4)
        sampler = cstring(data, pos + name_rel) if name_rel else ""
        palette = cstring(data, pos + palette_rel) if palette_rel else None
        texmap = u32(data, pos + 0x10)
        if texmap >= 8 or not sampler:
            raise ValueError(f"invalid texture binding: {name} {i}")
        lod_bias = struct.unpack_from(">f", data, pos + 0x28)[0]
        if not math.isfinite(lod_bias):
            raise ValueError("invalid LOD bias")
        samplers.append({
            "texmap": texmap, "name": sampler, "palette": palette,
            "wrap_s": u32(data, pos + 0x18), "wrap_t": u32(data, pos + 0x1C),
            "min_filter": u32(data, pos + 0x20), "mag_filter": u32(data, pos + 0x24),
            "lod_bias": round(lod_bias, 6),
        })
    by_map = {s["texmap"]: s["name"] for s in samplers}
    stages = [tev_stage(bp, i) for i in range(stage_count)]
    for stage in stages:
        stage["sampler"] = by_map.get(stage["texmap"]) if stage["texmap"] is not None else None
    material_flags = u32(data, base + 0x10)
    return {
        "name": name, "material_index": u32(data, base + 0x0C),
        "translucent": bool(material_flags & 0x80000000),
        "num_texgens": texture_gen_count, "num_channels": channel_count,
        "num_tev_stages": stage_count, "num_indirect_stages": ind_count,
        "light_set": signed_byte(data[base + 0x1D]),
        "fog_index": signed_byte(data[base + 0x1E]),
        "samplers": samplers, "tev": stages,
        "alpha_compare_bp": f"{bp[0xF3]:06x}" if 0xF3 in bp else None,
        "z_mode_bp": f"{bp[0x40]:06x}" if 0x40 in bp else None,
        "blend_bp": f"{bp[0x41]:06x}" if 0x41 in bp else None,
        "bp_register_count": len(bp),
    }


def model_materials(data: bytes, offset: int, model_name: str) -> dict:
    revision = u32(data, offset + 8)
    if revision not in (8, 9, 11):
        raise ValueError(f"unsupported MDL0 revision {revision}")
    rel = u32(data, offset + 16 + 8 * 4)
    group = dictionary(data, offset + rel) if rel else []
    if len(group) > MAX_MATERIALS:
        raise ValueError("too many materials")
    return {
        "name": model_name, "revision": revision,
        "materials": [material_entry(data, loc, name, revision) for name, loc in group],
    }


def pat0_clip(data: bytes, base: int, name: str) -> dict:
    version = u32(data, base + 8)
    if version not in (3, 4):
        raise ValueError(f"unsupported PAT0 revision {version}")
    off = 0x28 if version == 4 else 0x24
    duration = u16(data, base + off + 8)
    mat_count = u16(data, base + off + 0x0A)
    texname_count = u16(data, base + off + 0x0C)
    if texname_count > 512 or mat_count > MAX_MATERIALS:
        raise ValueError("PAT0 materials/textures over limit")
    name_table = base + u32(data, base + 0x14)
    texnames = [cstring(data, name_table + u32(data, name_table + 4 * i))
                for i in range(texname_count)]
    group = dictionary(data, base + u32(data, base + 0x10))
    if len(group) != mat_count:
        raise ValueError("PAT0 materials count mismatch")
    animated: list[dict] = []
    for mat_name, pos in group:
        flags = u32(data, pos + 4)
        cursor = pos + 8
        for slot in range(8):
            field = (flags >> (4 * slot)) & 15
            if not field & 1:
                continue
            const, tex_exists, pal_exists = bool(field & 2), bool(field & 4), bool(field & 8)
            if not tex_exists or pal_exists:
                raise ValueError("unsupported PAT0 texture/palette animation")
            if const:
                frames = [(0., u16(data, cursor))]
            else:
                track = pos + u32(data, cursor)
                count = u16(data, track)
                if count > 512:
                    raise ValueError("PAT0 keyframes count exceeds limit")
                frames = [(round(struct.unpack_from(">f", data, track + 8 + i * 8)[0], 3),
                           u16(data, track + 12 + i * 8))
                          for i in range(count)]
            cursor += 4
            if any(not math.isfinite(t) or i >= len(texnames) for t, i in frames):
                raise ValueError("PAT0 references texture not in its name table")
            animated.append({
                "material": mat_name, "texmap": slot,
                "frames": [{"frame": t, "texture": texnames[i]} for t, i in frames],
            })
    return {"name": name, "duration": duration, "texture_names": texnames,
            "animations": animated}


def srt0_clip(data: bytes, base: int, name: str) -> dict:
    version = u32(data, base + 8)
    if version not in (4, 5):
        raise ValueError(f"unsupported SRT0 revision {version}")
    off = 0x18 if version == 5 else 0x14
    duration = u16(data, base + off + 8)
    count = u16(data, base + off + 0x0A)
    if count > MAX_MATERIALS:
        raise ValueError("SRT0 material count exceeds limit")
    group = dictionary(data, base + u32(data, base + 0x10))
    if len(group) != count:
        raise ValueError("SRT0 material count mismatch")
    animated: list[dict] = []
    for material, pos in group:
        flags = u32(data, pos + 4) | ((u32(data, pos + 8) & 15) << 8)
        indices = [i for i in range(12) if flags & (1 << i)]
        if indices:
            bounds(data, pos + 12, 4 * len(indices))
            animated.append({"material": material, "tex_matrix_indices": indices})
    return {"name": name, "duration": duration, "animations": animated}


def cmpr_level0_rgba(pixels: bytes, width: int, height: int) -> bytes:
    """Decode GX CMPR's 8x8 macrotiles / four 4x4 DXT1 blocks.

    This is an independent, limited diagnostic decoder, not a texture-format
    conversion path used by the Vita renderer. It exists to fingerprint
    expected input pixels for a later CPU/GXM upload comparison.
    """
    if not 1 <= width <= 4096 or not 1 <= height <= 4096:
        raise ValueError("unsupported CMPR dimensions")
    across = (width + 7) // 8
    down = (height + 7) // 8
    source = extent(pixels, 0, across * down * 32)
    rgba = bytearray(width * height * 4)

    def color565(value: int) -> tuple[int, int, int]:
        return ((value >> 11 & 31) * 255 // 31,
                (value >> 5 & 63) * 255 // 63,
                (value & 31) * 255 // 31)

    for tile_y in range(down):
        for tile_x in range(across):
            base = (tile_y * across + tile_x) * 32
            for sub in range(4):
                block = source[base + sub * 8:base + sub * 8 + 8]
                a, b = struct.unpack_from(">HH", block)
                rgb0, rgb1 = color565(a), color565(b)
                if a > b:
                    colors = [(*rgb0, 255), (*rgb1, 255),
                              (*( (rgb0[c] * 2 + rgb1[c]) // 3 for c in range(3)), 255),
                              (*( (rgb0[c] + rgb1[c] * 2) // 3 for c in range(3)), 255)]
                else:
                    colors = [(*rgb0, 255), (*rgb1, 255),
                              (*((rgb0[c] + rgb1[c]) // 2 for c in range(3)), 255),
                              (0, 0, 0, 0)]
                for y in range(4):
                    dest_y = tile_y * 8 + (sub // 2) * 4 + y
                    if dest_y >= height:
                        continue
                    row = block[4 + y]
                    for x in range(4):
                        dest_x = tile_x * 8 + (sub % 2) * 4 + x
                        if dest_x >= width:
                            continue
                        rgba[(dest_y * width + dest_x) * 4:
                             (dest_y * width + dest_x) * 4 + 4] = bytes(
                                 colors[(row >> (6 - 2 * x)) & 3])
    return bytes(rgba)


def cmpr_diagnostics(pixels: bytes, width: int, height: int) -> dict:
    rgba = cmpr_level0_rgba(pixels, width, height)
    total = width * height
    visible = 0
    colorful = 0
    white = 0
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i:i + 4]
        if a == 0:
            continue
        visible += 1
        if r < 240 or g < 240 or b < 240:
            colorful += 1
        else:
            white += 1
    return {
        "decoded_level0_sha256": hashlib.sha256(rgba).hexdigest(),
        "opaque_or_semitransparent_pixels": visible,
        "nonwhite_visible_pixels": colorful,
        "nearwhite_visible_pixels": white,
        "transparent_pixels": total - visible,
    }


def texture_metadata(data: bytes, archive_name: str) -> list[dict]:
    result = []
    for t in textures(data, archive_name):
        meta = {
            "name": t.name.split("!")[-1],
            "width": t.width, "height": t.height,
            "gx_format": t.gx_format, "palette_format": t.palette_format,
            "mipmap_levels": t.levels, "pixel_bytes": len(t.pixels),
            "pixel_sha256": hashlib.sha256(t.pixels).hexdigest(),
            "palette_sha256": hashlib.sha256(t.palette).hexdigest() if t.palette else None,
            "palette_bytes":len(t.palette),
            "palette_fnv64":source_fnv64(t.palette) if t.palette else None,
            "vita_format": GX_FORMAT[t.gx_format],
            "source_fingerprints": source_fingerprints(t.pixels,t.width,t.height,
                                                       t.gx_format,t.levels),
        }
        if t.gx_format == 14:
            meta["cmpr_level0"] = cmpr_diagnostics(t.pixels, t.width, t.height)
        result.append(meta)
    return result


def tev_profile(material: dict) -> dict[str, str]:
    """Stable source-side descriptor for a GXM capture produced after this audit."""
    stages = material["tev"]
    return {
        "tev_map_coords": "".join(
            f"{(s['texmap'] if s['texmap'] is not None else 255):02x}{s['texcoord']:02x}"
            for s in stages),
        "tev_color_args": "".join(
            "".join(f"{arg:x}" for arg in s["color_args_abcd"]) for s in stages),
        "tev_alpha_args": "".join(
            "".join(f"{arg:x}" for arg in s["alpha_args_abcd"]) for s in stages),
    }


def brres_inventory(data: bytes, leaf_name: str) -> dict:
    result: dict = {
        "leaf": leaf_name.rsplit("!", 1)[-1],
        "sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data), "textures": [], "models": [], "pat0": [], "srt0": [],
    }
    result["textures"] = texture_metadata(data, leaf_name)
    for _, name, offset in sections(data):
        header = data[offset:offset + 4]
        if header == b"MDL0":
            result["models"].append(model_materials(data, offset, name))
        elif header == b"PAT0":
            result["pat0"].append(pat0_clip(data, offset, name))
        elif header == b"SRT0":
            result["srt0"].append(srt0_clip(data, offset, name))
    if len(result["pat0"]) > MAX_CLIPS or len(result["srt0"]) > MAX_CLIPS:
        raise ValueError("too many animation clips")
    return result


def trace_summary(path: Path | None) -> dict | None:
    if not path:
        return None
    with path.open(newline="") as handle:
        rows = [r for r in csv.DictReader(handle) if r.get("type") == "2"]
    stages_masks = collections.Counter((int(r["tev_stages"]), int(r["texture_mask"]))
                                       for r in rows)
    has_stage_signatures = bool(rows and "tev_map_coords" in rows[0])
    selected = []
    for row in rows:
        try:
            x0, y0, x1, y1 = (float(row.get("bounds_" + k, 0)) for k in
                              ("x0", "y0", "x1", "y1"))
        except (TypeError, ValueError):
            continue
        # Broad bounding-box overlap only; this DOES NOT prove pixel ownership
        # or associate a draw to any particular BRRES material.
        if row.get("bounds_valid") == "1" and x0 < 570 and x1 > 410 and y0 < 450 and y1 > 305:
            selected.append({
                "draw": int(row["draw"]), "vertices": int(row["vertices"]),
                "tev_stages": int(row["tev_stages"]),
                "texture_mask": int(row["texture_mask"]),
                "fragment_shader": row["fragment_shader"],
                "tev_profile": {key: row.get(key, "") for key in
                                ("tev_map_coords", "tev_color_args", "tev_alpha_args")}
                               if has_stage_signatures else None,
                "texture_handles": {str(i): int(row["tex_handle" + str(i)])
                                    for i in range(8)
                                    if row.get("tex_handle" + str(i), "0").isdigit()
                                    and int(row["tex_handle" + str(i)])},
            })
    return {
        "trace": path.name,
        "total_draw_rows": len(rows),
        "tev_signature_available": has_stage_signatures,
        "draws_by_stages_texture_mask": [
            {"tev_stages": k[0], "texture_mask": k[1], "draws": n}
            for k, n in sorted(stages_masks.items())
        ],
        "projected_region": [410, 305, 570, 450],
        "candidates": selected,
        "limitation": "Bounding-box overlap is NOT BRRES material ownership; "
                      "scene may contain occluded pixels, sprites or other models.",
    }


def compare_tev_profiles(report: dict) -> dict:
    """Match signatures, never infer ownership merely from screen overlap."""
    source: dict[tuple[str, str, str], list[dict]] = {}
    for archive in report["archives"]:
        for brres in archive["brres"]:
            if "driver_model" not in brres["leaf"]:
                continue
            for model in brres["models"]:
                for material in model["materials"]:
                    profile = tev_profile(material)
                    key = tuple(profile[k] for k in (
                        "tev_map_coords", "tev_color_args", "tev_alpha_args"))
                    source.setdefault(key, []).append({
                        "archive": archive["path"], "model": model["name"],
                        "material": material["name"],
                    })
    trace = report.get("trace")
    if not trace or not trace["tev_signature_available"]:
        return {
            "matched": False, "reason": "TRACE lacks TEV stage descriptors; "
                                        "recapture after GXM diagnostic extension",
            "source_driver_profiles": [
                {"profile": dict(zip((
                    "tev_map_coords", "tev_color_args", "tev_alpha_args"), key)),
                 "materials": value}
                for key, value in source.items()
            ],
        }
    candidates = []
    for row in trace["candidates"]:
        profile = row.get("tev_profile")
        if not profile:
            continue
        key = tuple(profile[k] for k in (
            "tev_map_coords", "tev_color_args", "tev_alpha_args"))
        matches = source.get(key)
        if matches:
            candidates.append({"draw": row["draw"],
                               "fragment_shader": row["fragment_shader"],
                               "candidate_materials": matches,
                               "limitation": "Same TEV profile does not identify texture contents "
                                             "or prove pixel ownership."})
    return {
        "matched": bool(candidates), "matched_candidate_draws": candidates,
        "source_driver_profiles": [
            {"profile": dict(zip((
                "tev_map_coords", "tev_color_args", "tev_alpha_args"), key)),
             "materials": value}
            for key, value in source.items()
        ],
    }


def read_texture_trace(path: Path) -> dict[int, dict]:
    """Read the optional, per-frame GXM texture table (never raw GPU assets)."""
    required={"handle","source_fnv64","source_hash_bytes","source_bytes",
              "palette_fnv64","palette_hash_bytes","palette_format",
              "width","height","gx_format","mip_count"}
    result={}
    with path.open(newline="") as f:
        reader=csv.DictReader(f)
        if not required.issubset(reader.fieldnames or ()):
            raise ValueError("missing GXM texture provenance columns")
        for row in reader:
            handle=int(row["handle"])
            if not 0<handle<=0xFFFFFFFF or handle in result or len(result)>=4096:
                raise ValueError("invalid or duplicate texture handle")
            for key in ("source_hash_bytes","source_bytes","width","height",
                        "gx_format","mip_count","palette_hash_bytes","palette_format"):
                row[key]=int(row[key])
            if row["source_hash_bytes"]>SOURCE_HASH_LIMIT or row["source_hash_bytes"]>row["source_bytes"]:
                raise ValueError("invalid source fingerprint length")
            if row["palette_hash_bytes"]>SOURCE_HASH_LIMIT:
                raise ValueError("invalid palette fingerprint length")
            if row["width"]>4096 or row["height"]>4096:
                raise ValueError("invalid GXM texture dimensions")
            source_hash=row["source_fnv64"].lower()
            if len(source_hash)!=16 or any(c not in "0123456789abcdef" for c in source_hash):
                raise ValueError("invalid source FNV hash")
            row["source_fnv64"]=source_hash
            palette_hash=row["palette_fnv64"].lower()
            if len(palette_hash)!=16 or any(c not in "0123456789abcdef" for c in palette_hash):
                raise ValueError("invalid palette FNV hash")
            row["palette_fnv64"]=palette_hash
            result[handle]=row
    return result


def compare_texture_provenance(report: dict,draws: Path | None,
                               textures_csv: Path | None) -> dict:
    """Find actual PIXEL-SOURCE matches; TEV signatures refine, but do not prove ownership."""
    if not draws or not textures_csv:
        return {"available":False,
                "reason":"No synchronized GXM draw and texture provenance CSV; "
                         "rebuild the opt-in trace and capture both files."}
    table=read_texture_trace(textures_csv)
    # Multiple archives/materials can legitimately contain identical compressed
    # GX data. Keep all names and never invent a 1:1 owner from a hash alone.
    sources=collections.defaultdict(list)
    profiles={}
    for archive in report["archives"]:
        for b in archive["brres"]:
            if "driver_model" not in b["leaf"]:
                continue
            for tex in b["textures"]:
                for prefix in tex["source_fingerprints"]:
                    key=(tex["vita_format"],tex["width"],tex["height"],
                         prefix["source_bytes"],prefix["source_hash_bytes"],
                         prefix["source_fnv64"],
                         tex.get("palette_format",0),
                         min(tex.get("palette_bytes",0),SOURCE_HASH_LIMIT),
                         tex.get("palette_fnv64") or "0000000000000000")
                    label={"archive":archive["path"],"texture":tex["name"],
                           "mip_count":prefix["mip_count"]}
                    if label not in sources[key]:
                        sources[key].append(label)
            for model in b["models"]:
                for material in model["materials"]:
                    key=tuple(tev_profile(material)[k] for k in
                              ("tev_map_coords","tev_color_args","tev_alpha_args"))
                    profiles.setdefault(key,[]).append({
                        "archive":archive["path"],"model":model["name"],
                        "material":material["name"],
                    })
    handles={}
    for handle,t in table.items():
        lookup=(t["gx_format"],t["width"],t["height"],
                t["source_bytes"],t["source_hash_bytes"],t["source_fnv64"],
                t["palette_format"],t["palette_hash_bytes"],t["palette_fnv64"])
        identities=sources.get(lookup,[]) if t["source_hash_bytes"] else []
        handles[handle]={
            "source_hash_present":bool(t["source_hash_bytes"]),
            "gx_format":t["gx_format"],"width":t["width"],"height":t["height"],
            "source_id":t.get("source_id"),"source_revision":t.get("source_revision"),
            "source_fnv64":t["source_fnv64"],
            "identities":identities,
        }
    matched_draws=[]
    signature_only=[]
    with draws.open(newline="") as f:
        for row in csv.DictReader(f):
            if row.get("type")!="2":
                continue
            if all(row.get(k) for k in ("tev_map_coords","tev_color_args","tev_alpha_args")):
                key=tuple(row[k] for k in
                          ("tev_map_coords","tev_color_args","tev_alpha_args"))
                profile_matches=profiles.get(key,[])
            else:profile_matches=[]
            tex_slots={}
            for slot in range(8):
                raw=row.get(f"tex_handle{slot}","")
                if not raw or not raw.isdigit() or int(raw)==0:
                    continue
                handle=int(raw)
                found=handles.get(handle)
                tex_slots[str(slot)]={
                    "handle":handle,
                    "identities":found["identities"] if found else [],
                    "fingerprinted":found["source_hash_present"] if found else False,
                }
            has_matching_content=any(v["identities"] for v in tex_slots.values())
            if not profile_matches and not has_matching_content:
                continue
            entry={
                "draw":int(row["draw"]),
                "fragment_shader":row.get("fragment_shader",""),
                "tev_stages":int(row["tev_stages"]),
                "texture_mask":int(row["texture_mask"]),
                "tev_matches":profile_matches,
                "matched_texture_slots":tex_slots,
                "matched_by_encoded_texture":has_matching_content,
                "matched_by_tev":bool(profile_matches),
            }
            if has_matching_content:
                matched_draws.append(entry)
            else:
                signature_only.append(entry)
    return {
        "available":True,"texture_rows":len(table),
        "source_hashes_present":sum(1 for x in handles.values() if x["source_hash_present"]),
        "identified_source_textures":sum(1 for x in handles.values() if x["identities"]),
        "content_matched_draw_count":len(matched_draws),
        "tev_only_draw_count":len(signature_only),
        "content_matched_draws":matched_draws[:128],
        "tev_only_draws":signature_only[:128],
        "limitation":"A matching GX compressed-texture fingerprint identifies source bytes, "
                     "not final pixel visibility, material ownership or correct GX state. "
                     "PAT0 changes and EFB copies need separate interpretation.",
    }


def inventory(psarc: Path, archives: list[str], trace: Path | None = None,
              texture_trace: Path | None = None) -> dict:
    index = parse_psarc(psarc)
    by_name = {name: i + 1 for i, name in enumerate(index.paths)}
    output = {"format": "wic-pal-brres-audit-v1",
              "psarc_name": psarc.name, "asset_count": len(index.paths),
              "archives": [], "trace": trace_summary(trace)}
    with psarc.open("rb", buffering=0) as handle:
        for path in archives:
            idx = by_name.get(path)
            if idx is None:
                raise ValueError("source archive unavailable: " + path)
            entry_size = index.entries[idx].size
            if entry_size > MAX_ARCHIVE_ENTRY:
                raise ValueError(f"archive exceeds cap: {path} ({entry_size})")
            raw = b"".join(iter_entry(handle, index, idx))
            if len(raw) != entry_size:
                raise ValueError("truncated PSARC entry " + path)
            record = {"path": path, "size": len(raw),
                      "sha256": hashlib.sha256(raw).hexdigest(), "brres": []}
            for name, body in leaves(raw, path):
                if body[:4] == b"bres":
                    record["brres"].append(brres_inventory(body, name))
            if not record["brres"]:
                raise ValueError("no BRRES files in " + path)
            output["archives"].append(record)
    output["tev_trace_comparison"] = compare_tev_profiles(output)
    if trace and texture_trace is None:
        guessed=Path(str(trace).replace("-draws.csv","-textures.csv"))
        if guessed!=trace and guessed.is_file():
            texture_trace=guessed
    output["texture_trace_comparison"]=compare_texture_provenance(
        output,trace,texture_trace)
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--psarc", type=Path,
                        default=Path("build/vita-data/mariokart-avc.psarc"))
    parser.add_argument("--archive", action="append", dest="archives",
                        help="Exact path within PSARC; may be repeated")
    parser.add_argument("--trace", type=Path, help="Vita3K draw-capture CSV")
    parser.add_argument("--textures", type=Path,
                        help="Matching frame-N-textures.csv with encoded GX fingerprints")
    parser.add_argument("--output", type=Path,
                        default=Path("build/material-audit/mario-pal.json"))
    args = parser.parse_args()
    data = inventory(args.psarc,args.archives or list(DEFAULT_ASSETS),
                     args.trace,args.textures)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(data, indent=2) + "\n")
    print(json.dumps({
        "output": str(args.output), "archives": len(data["archives"]),
        "models": sum(len(b["models"]) for a in data["archives"] for b in a["brres"]),
        "materials": sum(len(m["materials"]) for a in data["archives"]
                         for b in a["brres"] for m in b["models"]),
        "textures": sum(len(b["textures"]) for a in data["archives"] for b in a["brres"]),
        "trace_candidates": len(data["trace"]["candidates"]) if data["trace"] else None,
        "content_matched_draws":data["texture_trace_comparison"].get(
            "content_matched_draw_count"),
    }))
    return 0


if __name__ == "__main__":
    sys.exit(main())
