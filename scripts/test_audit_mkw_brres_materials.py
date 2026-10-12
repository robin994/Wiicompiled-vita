"""Standalone tests for the PAL BRRES material auditor (no Nintendo assets needed)."""
from __future__ import annotations

import csv
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import audit_mkw_brres_materials as audit


def u32(buffer, offset, value):
    struct.pack_into(">I", buffer, offset, value)


def u16(buffer, offset, value):
    struct.pack_into(">H", buffer, offset, value)


def bp_write(register, value):
    return bytes([0x61]) + ((register << 24) | (value & 0xFFFFFF)).to_bytes(4, "big")


def sample_stage_bp():
    return (bp_write(0x28, 0x040049) + bp_write(0x29, 0x3BF052) +
            bp_write(0xC0, 0x08F8BA) + bp_write(0xC1, 0x08FFF0) +
            bp_write(0xC2, 0x08F804) + bp_write(0xC3, 0x08F2F0) +
            bp_write(0xC4, 0x08F810) + bp_write(0xC5, 0x08DFF0))


def synthetic_material() -> bytes:
    data = bytearray(0x1000)
    u32(data, 0, 0x700)
    u32(data, 0x0C, 2)
    data[0x14:0x18] = b"\x03\x01\x03\x00"
    u32(data, 0x28, 0x800)
    u32(data, 0x2C, 3)
    u32(data, 0x30, 0x418)
    u32(data, 0x3C, 0x4C4)
    data[0x4C4:0x4C4 + 10] = bp_write(0x40, 0x17) + bp_write(0x41, 0x34a0)
    u32(data, 0x800, 512)
    u32(data, 0x808, 1)
    data[0x820:0x820 + len(sample_stage_bp())] = sample_stage_bp()
    for index, name in enumerate(["mario_all", "lm_0", "lm_1"]):
        pos = 0x418 + index * 0x34
        storage = 0xa20 + index * 0x20
        u32(data, pos, storage - pos)
        u32(data, pos + 0x10, index)
        u32(data, pos + 0x18, 1)
        data[storage:storage + len(name) + 1] = name.encode() + b"\0"
    return bytes(data)


def setup_dictionary(data: bytearray, at: int, target: int,
                     name: str = "mario_eye_tx"):
    u32(data, at, 0x60)
    u32(data, at + 4, 1)
    u32(data, at + 24 + 8, 0x40)
    u32(data, at + 24 + 12, target - at)
    data[at + 0x40:at + 0x40 + len(name) + 1] = name.encode() + b"\0"


class MaterialAuditTests(unittest.TestCase):

    def test_encoded_source_fingerprint_matches_vita_fnv64(self):
        standalone_hash=audit.FNV_OFFSET
        for value in b"mario":
            standalone_hash=((standalone_hash^value)*audit.FNV_PRIME)&0xFFFFFFFFFFFFFFFF
        self.assertEqual(standalone_hash,0x907ed0a4e178860b)
        self.assertEqual(audit.source_fnv64(b"mario"),"907ed0a4e178860b")
        sample=b"mario"
        fingerprint=audit.source_fingerprints(sample+bytes(27),8,8,0,1)[0]
        self.assertEqual(fingerprint["source_hash_bytes"],32)
        self.assertEqual(fingerprint["source_bytes"],32)
        h=audit.FNV_OFFSET
        for value in sample+bytes(27):
            h=((h^value)*audit.FNV_PRIME)&0xFFFFFFFFFFFFFFFF
        self.assertEqual(fingerprint["source_fnv64"],f"{h:016x}")
        red_block=b"\xf8\x00\x07\xe0"+bytes(4)
        fingerprints=audit.source_fingerprints(red_block*4,8,8,14,1)
        self.assertEqual(len(fingerprints),1)
        self.assertEqual(fingerprints[0]["source_bytes"],32)

    def test_provenance_match_is_by_encoded_bytes_not_aabb(self):
        material=audit.material_entry(synthetic_material(),0,"mario_all_tx",11)
        profile=audit.tev_profile(material)
        raw=b"\xf8\x00\x07\xe0"+bytes(4)
        encoded=raw*4
        fingerprint=audit.source_fingerprints(encoded,8,8,14,1)[0]
        report={"archives":[{"path":"ma_kart-mr.szs","brres":[{
            "leaf":"driver_model.brres",
            "textures":[{
                "name":"mario_all","width":8,"height":8,"gx_format":14,"vita_format":10,
                "palette_format":0,"palette_bytes":0,"palette_fnv64":None,
                "source_fingerprints":[fingerprint],
            }],
            "models":[{"name":"model","materials":[material]}],
        }]}]}
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            draws=root/"draws.csv"
            textures=root/"textures.csv"
            with draws.open("w",newline="") as out:
                writer=csv.DictWriter(out,fieldnames=[
                    "type","draw","fragment_shader","tev_stages","texture_mask",
                    "tev_map_coords","tev_color_args","tev_alpha_args",
                    *[f"tex_handle{i}" for i in range(8)]])
                writer.writeheader()
                writer.writerow({"type":"2","draw":"73","fragment_shader":"hash",
                    "tev_stages":"3","texture_mask":"7",
                    "tex_handle0":"489",**profile})
            columns=["handle","source_id","palette_source_id","source_fnv64",
                     "palette_fnv64","source_hash_bytes","palette_hash_bytes",
                     "source_bytes","width","height","gx_format","palette_format",
                     "mip_count","source_revision","binding_source"]
            def make_table(hash_value):
                with textures.open("w",newline="") as out:
                    writer=csv.DictWriter(out,fieldnames=columns)
                    writer.writeheader()
                    writer.writerow({"handle":"489","source_id":"80001000",
                        "source_fnv64":hash_value,"source_hash_bytes":32,
                        "source_bytes":32,"width":8,"height":8,"gx_format":10,
                        "mip_count":1,"palette_fnv64":"0000000000000000",
                        "palette_hash_bytes":0,"palette_format":0})
            make_table(fingerprint["source_fnv64"])
            compare=audit.compare_texture_provenance(report,draws,textures)
            self.assertEqual(compare["content_matched_draw_count"],1)
            match=compare["content_matched_draws"][0]
            self.assertTrue(match["matched_by_encoded_texture"])
            self.assertTrue(match["matched_by_tev"])
            self.assertEqual(match["matched_texture_slots"]["0"]["identities"][0]["texture"],
                             "mario_all")
            make_table("0000000000000000")
            compare=audit.compare_texture_provenance(report,draws,textures)
            self.assertEqual(compare["content_matched_draw_count"],0)
            self.assertEqual(compare["tev_only_draw_count"],1)
            make_table(fingerprint["source_fnv64"])
            with textures.open("a",newline="") as out:
                writer=csv.DictWriter(out,fieldnames=columns)
                writer.writerow({"handle":"489","source_fnv64":fingerprint["source_fnv64"],
                    "source_hash_bytes":32,"source_bytes":32,"width":8,"height":8,
                    "gx_format":10,"mip_count":1,"palette_fnv64":"0000000000000000",
                    "palette_hash_bytes":0,"palette_format":0})
            with self.assertRaises(ValueError):
                audit.read_texture_trace(textures)

    def test_bp_stream_parses_xf_cp_and_padding(self):
        blob = (b"\x00" * 4 + bp_write(0x28, 0x040049) +
                b"\x10" + (0x00000004).to_bytes(4, "big") +
                b"\x01\x02\x03\x04" + b"\x08\x50\x00\x00\x00\x01" +
                bp_write(0x28, 0x040052) + b"\0" * 5)
        self.assertEqual(audit.gx_bp_commands(blob), {0x28: 0x040052})
        with self.assertRaises(ValueError):
            audit.gx_bp_commands(b"\x61\0")
        with self.assertRaises(ValueError):
            audit.gx_bp_commands(b"\xff")

    def test_tev_material_signature(self):
        raw = synthetic_material()
        result = audit.material_entry(raw, 0, "mario_all_tx", 11)
        self.assertEqual(result["num_tev_stages"], 3)
        self.assertEqual([s["texmap"] for s in result["tev"]], [1, 0, 2])
        self.assertEqual([s["texcoord"] for s in result["tev"]], [1, 0, 2])
        self.assertEqual([s["sampler"] for s in result["tev"]],
                         ["lm_0", "mario_all", "lm_1"])
        self.assertEqual([s["color_args_abcd"] for s in result["tev"]],
                         [[15, 8, 11, 10], [15, 8, 0, 4], [15, 8, 1, 0]])
        self.assertEqual(audit.tev_profile(result), {
            "tev_map_coords": "010100000202",
            "tev_color_args": "f8baf804f810",
            "tev_alpha_args": "777774576777",
        })

    def test_reject_bad_material_bounds_and_stage_counts(self):
        data = bytearray(synthetic_material())
        data[0x16] = 17
        with self.assertRaises(ValueError):
            audit.material_entry(bytes(data), 0, "bad", 11)
        data = bytearray(synthetic_material())
        u32(data, 0x28, 0xFFFFFF00)
        with self.assertRaises(ValueError):
            audit.material_entry(bytes(data), 0, "bad", 11)
        data = bytearray(synthetic_material())
        u32(data, 0x2C, 9)
        with self.assertRaises(ValueError):
            audit.material_entry(bytes(data), 0, "bad", 11)

    def test_pat0_and_srt0_named_matrix_tracks(self):
        pat = bytearray(0x280)
        pat[:4] = b"PAT0"
        u32(pat, 8, 3)
        u32(pat, 0x10, 0x100)
        u32(pat, 0x14, 0x200)
        u16(pat, 0x24 + 8, 12)
        u16(pat, 0x24 + 0x0A, 1)
        u16(pat, 0x24 + 0x0C, 1)
        setup_dictionary(pat, 0x100, 0x180)
        u32(pat, 0x184, 0x7)
        u16(pat, 0x188, 0)
        u32(pat, 0x200, 0x20)
        pat[0x220:0x220 + 12] = b"mario_eye.0\0"
        data = audit.pat0_clip(bytes(pat), 0, "wink")
        self.assertEqual(data["animations"][0]["frames"][0]["texture"],
                         "mario_eye.0")
        self.assertEqual(data["animations"][0]["texmap"], 0)

        srt = bytearray(0x240)
        srt[:4] = b"SRT0"
        u32(srt, 8, 4)
        u32(srt, 0x10, 0x100)
        u16(srt, 0x14 + 8, 1)
        u16(srt, 0x14 + 0x0A, 1)
        setup_dictionary(srt, 0x100, 0x180)
        u32(srt, 0x184, 0x1)
        u32(srt, 0x18C, 0x40)
        data = audit.srt0_clip(bytes(srt), 0, "left")
        self.assertEqual(data["animations"],
                         [{"material": "mario_eye_tx", "tex_matrix_indices": [0]}])

    def test_cmpr_colored_and_white(self):
        red_block = b"\xf8\0\x07\xe0" + b"\0" * 4
        rgba = audit.cmpr_level0_rgba(red_block * 4, 8, 8)
        self.assertEqual(len(rgba), 256)
        self.assertEqual(rgba[:4], bytes([255, 0, 0, 255]))
        self.assertEqual(rgba[-4:], bytes([255, 0, 0, 255]))
        stats = audit.cmpr_diagnostics(red_block * 4, 8, 8)
        self.assertEqual(stats["nonwhite_visible_pixels"], 64)
        white_block = b"\xff\xff\0\0" + b"\0" * 4
        stats = audit.cmpr_diagnostics(white_block * 4, 8, 8)
        self.assertEqual(stats["nearwhite_visible_pixels"], 64)
        with self.assertRaises(ValueError):
            audit.cmpr_level0_rgba(b"\0" * 8, 8, 8)

    def test_match_tevs_without_identifying_pixel_ownership(self):
        material = audit.material_entry(synthetic_material(), 0, "mario_all_tx", 11)
        profile = audit.tev_profile(material)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "capture.csv"
            fields = ["type", "tev_stages", "texture_mask", "bounds_valid",
                      "bounds_x0", "bounds_y0", "bounds_x1", "bounds_y1",
                      "draw", "vertices", "fragment_shader"] + list(profile) + [
                          f"tex_handle{i}" for i in range(8)]
            row = {"type": "2", "tev_stages": "3", "texture_mask": "7",
                   "bounds_valid": "1", "bounds_x0": "435",
                   "bounds_y0": "300", "bounds_x1": "550",
                   "bounds_y1": "435", "draw": "5",
                   "vertices": "140", "fragment_shader": "deadbeef", **profile}
            with path.open("w", newline="") as out:
                writer = csv.DictWriter(out, fieldnames=fields)
                writer.writeheader()
                writer.writerow(row)
            report = {"archives": [{"path": "ma_kart-mr.szs",
                         "brres": [{"leaf": "driver_model.brres",
                                    "models": [{"name": "model",
                                                "materials": [material]}]}]}],
                      "trace": audit.trace_summary(path)}
            matched = audit.compare_tev_profiles(report)
            self.assertTrue(matched["matched"])
            self.assertEqual(matched["matched_candidate_draws"][0]["draw"], 5)
            # Wrong stage signature is never matched even with a perfect AABB.
            report["trace"]["candidates"][0]["tev_profile"]["tev_color_args"] = "0000"
            self.assertFalse(audit.compare_tev_profiles(report)["matched"])

    @unittest.skipUnless(os.environ.get("WIC_MKW_PAL_PSARC"),
                         "requires user-owned PAL PSARC; set WIC_MKW_PAL_PSARC to opt in")
    def test_pal_driver_materials_from_user_disc(self):
        report = audit.inventory(Path(os.environ["WIC_MKW_PAL_PSARC"]),
                                 list(audit.DEFAULT_ASSETS))
        self.assertEqual(len(report["archives"]), 2)
        models = []
        for entry in report["archives"]:
            driver = next(b for b in entry["brres"] if b["leaf"].endswith("driver_model.brres"))
            self.assertEqual(len(driver["pat0"]), 8)
            self.assertEqual(len(driver["srt0"]), 2)
            self.assertEqual(len(driver["textures"]), 12)
            mario_all = next(m for m in driver["models"][0]["materials"]
                             if m["name"] == "mario_all_tx")
            self.assertEqual(mario_all["num_tev_stages"], 3)
            self.assertEqual([s["sampler"] for s in mario_all["tev"]],
                             ["lm_0", "mario_all", "lm_1"])
            tex = next(t for t in driver["textures"] if t["name"] == "mario_all")
            self.assertGreater(tex["cmpr_level0"]["nonwhite_visible_pixels"], 30000)
            models.append(driver["sha256"])
        self.assertEqual(models[0], models[1])


if __name__ == "__main__":
    unittest.main()
