#!/usr/bin/env python3
"""Convert a Mario Kart PSARC's THP video components to independent H.264 AUs.

The THP frame envelope, source frame rate/count and ADPCM audio are retained.
Video components carry an explicit WAVC v1 header; these are NOT ordinary THP
files and require the WIC_HARDWARE_AVC runtime. No runtime extracted files or
second ISO are needed. Source archives are never overwritten.
"""
from __future__ import annotations

import argparse
import hashlib
from fractions import Fraction
import json
import mmap
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import time

from vita_psarc import create_psarc, iter_entry, parse_psarc, verify_psarc
from prepare_mkw_psarc import EXPECTED_EXECUTABLES

VIDEO_HEADER = struct.Struct(">4sIIIII")  # magic, version, width, height, ES bytes, flags
PROFILE = struct.Struct("<8sII")  # WICAVC01, version, converted file count
START = re.compile(b"\x00\x00(?:\x00)?\x01")


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(4 * 1024 * 1024), b""): h.update(chunk)
    return h.hexdigest()


def u32(data, pos): return struct.unpack_from(">I", data, pos)[0]


def thp_info(data):
    if len(data) < 80 or data[:4] != b"THP\0" or u32(data, 4) not in (0x10000, 0x11000):
        raise ValueError("unsupported THP header")
    info, table, first = (u32(data, p) for p in (32, 36, 40))
    if table or info < 48 or info + 28 > first or first > len(data) or first % 16:
        raise ValueError("unsupported THP component/offset table")
    count = u32(data, info)
    if not 1 <= count <= 2 or data[info+4:info+4+count] != bytes(range(count)):
        raise ValueError("expected video, optionally followed by audio")
    width, height = struct.unpack_from(">II", data, info+20)
    if not 16 <= width <= 704 or not 16 <= height <= 768 or width % 16 or height % 16:
        raise ValueError(f"unsupported geometry {width}x{height}")
    frames = u32(data, 20)
    fps = struct.unpack_from(">f", data, 16)[0]
    if not 0 < frames <= 100000 or not 0 < fps <= 60.01:
        raise ValueError("invalid frame count/rate")
    return dict(count=count, width=width, height=height, frames=frames, fps=fps, first=first)


def thp_frames(data, info):
    pos, size, previous = info["first"], u32(data, 24), None
    first_size = size
    for frame in range(info["frames"]):
        if size < 8+4*info["count"] or pos+size > len(data) or size % 32:
            raise ValueError(f"invalid THP frame extent {frame}")
        next_size, previous_size = struct.unpack_from(">II", data, pos)
        if previous is not None and previous_size != previous:
            raise ValueError(f"invalid previous frame size {frame}")
        sizes = struct.unpack_from(">"+"I"*info["count"], data, pos+8)
        at = pos+8+4*info["count"]
        if sum(sizes) > size-(at-pos) or any(n % 4 for n in sizes):
            raise ValueError("invalid THP component lengths")
        components = []
        for length in sizes:
            components.append((at, length)); at += length
        yield pos, size, components
        previous, pos, size = size, pos+size, next_size
    if size != first_size or pos != len(data) or u32(data, 44) != pos-previous:
        raise ValueError("invalid THP final extent/loop size")


def access_units(data: bytes):
    marks = list(START.finditer(data))
    if not marks or marks[0].start() != 0:
        raise ValueError("invalid Annex B stream")
    starts = [m.start() for m in marks if m.end() < len(data) and data[m.end()] & 31 == 9]
    if not starts or starts[0] != 0:
        raise ValueError("H.264 access units need AUD delimiters")
    starts.append(len(data))
    for first, last in zip(starts, starts[1:]):
        unit = data[first:last]
        types = [unit[m.end()] & 31 for m in START.finditer(unit) if m.end() < len(unit)]
        if not {7, 8, 9, 5}.issubset(types) or 1 in types or len(unit) > 1024*1024-24:
            raise ValueError("each AU must contain SPS/PPS/AUD and an independent IDR")
        for m in START.finditer(unit):
            if unit[m.end()] & 31 == 7 and (m.end()+1>=len(unit) or unit[m.end()+1] != 66):
                raise ValueError("expected AVC Baseline profile")
        yield unit


def convert_clip(source: Path, output: Path, ffmpeg: str, crf: int, threads: int):
    started = time.monotonic()
    with source.open("rb") as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as original:
        info = thp_info(original)
        frames = list(thp_frames(original, info))
        elementary = output.with_suffix(".h264")
        # Keep original full-range YUV values. No frame dropping or audio resampling.
        command = [ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
            # Reconstruct input video timestamps from the floating THP header
            # rate, since its demuxer advertises an integer stream time base.
            "-r", str(Fraction(str(round(info["fps"],3)))),
            "-i", str(source), "-map", "0:v:0", "-an", "-vf",
            "scale=in_range=full:out_range=full,format=yuv420p", "-fps_mode", "passthrough",
            "-c:v", "libx264", "-preset", "fast", "-crf", str(crf), "-threads", str(threads),
            "-profile:v", "baseline", "-level:v", "3.2", "-color_range", "pc",
            "-x264-params", "keyint=1:min-keyint=1:scenecut=0:bframes=0:ref=1:aud=1:repeat-headers=1:fullrange=on:force-cfr=1",
            "-f", "h264", str(elementary)]
        subprocess.run(command, check=True)
        units = list(access_units(elementary.read_bytes()))
        if len(units) != info["frames"]: raise ValueError("conversion changed the frame count")
        sizes = []
        for unit, (_, _, parts) in zip(units, frames):
            video_size = (VIDEO_HEADER.size+len(unit)+3) & ~3
            size = 8+4*info["count"]+video_size+sum(n for _, n in parts[1:])
            sizes.append((size+31) & ~31)
        prefix = bytearray(original[:info["first"]])
        for offset, value in ((8,max(sizes)),(24,sizes[0]),(28,sum(sizes)),
                              (44,info["first"]+sum(sizes[:-1]))):
            struct.pack_into(">I", prefix, offset, value)
        original_audio, converted_audio = hashlib.sha256(), hashlib.sha256()
        with output.open("wb") as target:
            target.write(prefix)
            for i, (unit, (_, _, parts)) in enumerate(zip(units, frames)):
                video = VIDEO_HEADER.pack(b"WAVC",1,info["width"],info["height"],len(unit),1)+unit
                video += b"\0"*((-len(video)) % 4)
                previous = sizes[i-1] if i or u32(original,info["first"]+4) else 0
                record = bytearray(struct.pack(">II", sizes[(i+1)%len(sizes)], previous))
                record += struct.pack(">"+"I"*info["count"], len(video), *(n for _, n in parts[1:]))
                record += video
                for at, length in parts[1:]:
                    audio = original[at:at+length]
                    original_audio.update(audio); record += audio; converted_audio.update(audio)
                record += b"\0"*(sizes[i]-len(record)); target.write(record)
        if original_audio.digest() != converted_audio.digest(): raise ValueError("audio changed")
    with output.open("rb") as f, mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as converted:
        converted_info=thp_info(converted)
        if info != converted_info: raise ValueError("video metadata changed")
        for i, (_, _, parts) in enumerate(thp_frames(converted, info)):
            at, length = parts[0]
            magic, version, w, h, es, flags = VIDEO_HEADER.unpack_from(converted, at)
            if (magic,version,w,h,flags) != (b"WAVC",1,info["width"],info["height"],1) or es+24 > length:
                raise ValueError("invalid converted component")
            if converted[at+24:at+24+es] != units[i]: raise ValueError("H.264 payload changed")
    elementary.unlink()
    return {**info,"source_bytes":source.stat().st_size,"converted_bytes":output.stat().st_size,
            "source_sha256":sha(source),"converted_sha256":sha(output),
            "audio_sha256":original_audio.hexdigest(),"audio_byte_identical":True,
            "seconds":time.monotonic()-started,"encoder_command":command}


def patch_index(data: bytes, sizes: dict[str,int]):
    if len(data)<40 or data[:8]!=b"WICIDX01" or struct.unpack_from("<I",data,8)[0]!=1:
        raise ValueError("invalid disc index")
    out=bytearray(data); pos=40; changed={}
    for _ in range(struct.unpack_from("<I",data,12)[0]):
        offset,size,length,flags=struct.unpack_from("<QIHH",data,pos)
        path=data[pos+16:pos+16+length].decode("utf8")
        name="disc"+path
        if name in sizes:
            new=sizes[name]
            if flags or new>size: raise ValueError("converted video cannot grow its partition extent")
            struct.pack_into("<I",out,pos+8,new); changed[offset]=(size,new)
        pos+=16+length
    if pos!=len(data) or len(changed)!=len(sizes): raise ValueError("index video mismatch")
    return bytes(out),changed


def patch_fst(data: bytes, changed: dict[int,tuple[int,int]]):
    out=bytearray(data); count=u32(data,8); seen=set()
    if count*12>len(data): raise ValueError("invalid FST")
    for i in range(1,count):
        pos=i*12
        if data[pos] != 0: continue
        offset=u32(data,pos+4)*4
        if offset in changed:
            old,new=changed[offset]
            if u32(data,pos+8)!=old: raise ValueError("FST/index length mismatch")
            struct.pack_into(">I",out,pos+8,new); seen.add(offset)
    if seen!=set(changed): raise ValueError("video missing from FST")
    return bytes(out)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source",type=Path)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--ffmpeg",default="ffmpeg")
    parser.add_argument("--crf",type=int,default=18)
    parser.add_argument("--threads",type=int,default=4)
    parser.add_argument("--clip",action="store_true",help="Convert one standalone THP for host tests")
    parser.add_argument("--path",action="append",help="Only these archive paths; default: all THPs")
    parser.add_argument("--native-probe",type=Path)
    args=parser.parse_args()
    source=args.source.resolve(); output=args.output.absolute()
    if output.exists() or output.is_symlink() or source==output.resolve(): raise ValueError("output must be new")
    if not 0<=args.crf<=30 or not 1<=args.threads<=16: raise ValueError("invalid encoder options")
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mkw-avc-",dir=output.parent) as temporary:
        work=Path(temporary)
        if args.clip:
            candidate=work/"converted.thp"
            report=convert_clip(source,candidate,args.ffmpeg,args.crf,args.threads)
        else:
            archive=parse_psarc(source); root=work/"input"; root.mkdir()
            paths=set(archive.paths)
            if "disc/native-video-profile.bin" in paths: raise ValueError("source is already converted")
            required={"disc/index.bin","disc/sys/fst.bin","disc/sys/main.dol","disc/rel/StaticR.rel"}
            if not required.issubset(paths): raise ValueError("not a Mario Kart disc PSARC")
            selected={p for p in paths if p.lower().endswith(".thp")}
            if args.path:
                if not set(args.path).issubset(selected): raise ValueError("unknown video path")
                selected=set(args.path)
            if not selected: raise ValueError("no video files")
            with source.open("rb") as f:
                for i,name in enumerate(archive.paths,1):
                    dest=root/name; dest.parent.mkdir(parents=True,exist_ok=True)
                    with dest.open("wb") as target:
                        for chunk in iter_entry(f,archive,i): target.write(chunk)
            original={p:sha(root/p) for p in archive.paths}
            if (root/"disc/index.bin").read_bytes()[32:40].rstrip(b"\0")!=b"RMCP01":
                raise ValueError("expected RMCP01 disc profile")
            for name,expected in EXPECTED_EXECUTABLES.items():
                if original["disc/"+name]!=expected:raise ValueError("executable differs from translated RMCP01: "+name)
            videos=[]; sizes={}
            for name in sorted(selected):
                dest=root/name; converted=work/"converted.thp"
                result=convert_clip(dest,converted,args.ffmpeg,args.crf,args.threads)
                result["path"]=name; videos.append(result); sizes[name]=converted.stat().st_size
                if sizes[name]>dest.stat().st_size: raise ValueError("conversion grew a video")
                os.replace(converted,dest)
                print(f"{name}: {result['source_bytes']} -> {sizes[name]} bytes",flush=True)
            index=root/"disc/index.bin"; fst=root/"disc/sys/fst.bin"
            patched,changed=patch_index(index.read_bytes(),sizes); index.write_bytes(patched)
            fst.write_bytes(patch_fst(fst.read_bytes(),changed))
            (root/"disc/native-video-profile.bin").write_bytes(PROFILE.pack(b"WICAVC01",1,len(videos)))
            altered=selected|{"disc/index.bin","disc/sys/fst.bin"}
            for p,h in original.items():
                if p not in altered and sha(root/p)!=h: raise ValueError("non-video asset changed: "+p)
            candidate=work/"converted.psarc"
            created=create_psarc(candidate,[p for p in root.iterdir() if p.is_dir()])
            verify=verify_psarc(candidate,full=True)
            created["archive"]=str(output); verify["archive"]=str(output)
            if args.native_probe:
                subprocess.run([str(args.native_probe.resolve()),"verify-root",str(candidate),str(root)],check=True)
            report={"format":"wic-avc-video-v1","requires_runtime_option":"WIC_HARDWARE_AVC=ON",
                "video_codec":"H264 Baseline all-intra Annex B, full-range YUV420",
                "runtime_extracted_files":False,"runtime_iso_required":False,"hardware_validated":False,
                "source_sha256":sha(source),"source_bytes":source.stat().st_size,"videos":videos,
                "verified_executables":{name:original["disc/"+name] for name in EXPECTED_EXECUTABLES},
                "unmodified_assets":len(original)-len(altered),"create":created,"verify":verify}
        report.update(output_bytes=candidate.stat().st_size,output_sha256=sha(candidate),output=str(output))
        report_path=output.with_suffix(output.suffix+".json")
        if report_path.exists(): raise ValueError("report output must be new")
        report_path.write_text(json.dumps(report,indent=2)+"\n")
        os.replace(candidate,output)
        print(json.dumps({k:report[k] for k in ("output","output_bytes","output_sha256")}),flush=True)


if __name__=="__main__":
    try: main()
    except (OSError,ValueError,struct.error,subprocess.CalledProcessError) as error:
        raise SystemExit("Video conversion failed: "+str(error))
