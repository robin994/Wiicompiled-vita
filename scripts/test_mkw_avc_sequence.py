#!/usr/bin/env python3
"""Compare host-decoded pixels before/after closing sampled independent AVC AUs.

Uses the paired PSARC directly. This cannot prove Vita firmware output.
"""
import argparse
import hashlib
import itertools
import json
import struct
import subprocess
import tempfile
from pathlib import Path
from convert_mkw_video import VIDEO_HEADER, thp_info, thp_frames
from vita_psarc import parse_psarc, iter_entry


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive',type=Path)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    archive=parse_psarc(args.archive)
    records=[]
    with args.archive.open('rb') as source,tempfile.TemporaryDirectory(prefix='wic-avc-sequence-') as directory:
        for index,name in enumerate(archive.paths,1):
            if not name.lower().endswith('.thp'): continue
            data=bytearray();parts=None
            for chunk in iter_entry(source,archive,index):
                data.extend(chunk)
                if len(data)>4*1024*1024: raise ValueError('sample exceeds bounded extent '+name)
                try:
                    info=thp_info(data)
                    parts=list(itertools.islice(thp_frames(data,info),min(3,info['frames'])))
                    break
                except ValueError: pass  # First complete frames have not arrived yet.
            if not parts: raise ValueError('no sampled frames '+name)
            units=[]
            for _,_,components in parts:
                at,size=components[0]
                magic,version,w,h,n,flags=VIDEO_HEADER.unpack_from(data,at)
                if (magic,version,w,h,flags)!=(b'WAVC',1,info['width'],info['height'],1) or 24+n>size:
                    raise ValueError('invalid paired AVC component '+name)
                units.append(bytes(data[at+24:at+24+n]))
            results=[]
            for closed in [False,True]:
                path=Path(directory)/'sample.h264'
                path.write_bytes(b''.join(unit+(b'\0\0\0\1\x0a\x80' if closed else b'') for unit in units))
                decoded=subprocess.run(['ffmpeg','-v','error','-err_detect','explode','-f','h264','-i',str(path),
                    '-map','0:v:0','-fps_mode','passthrough','-f','rawvideo','-pix_fmt','yuv420p','pipe:1'],
                    check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                if decoded.stderr: raise ValueError(decoded.stderr.decode(errors='replace'))
                if len(decoded.stdout)!=len(units)*info['width']*info['height']*3//2:
                    raise ValueError('sampled frame count changed '+name)
                results.append(hashlib.sha256(decoded.stdout).hexdigest())
            if results[0]!=results[1]: raise ValueError('EOS changed decoded pixels '+name)
            records.append({'path':name,'frames':len(units),'width':info['width'],'height':info['height'],
                'pixels_sha256':results[0],'sequence_closed_pixels_identical':True})
            print(name,len(units),'frames, identical pixels PASS',flush=True)
    args.report.write_text(json.dumps({'videos':records,'sampled_frames':sum(r['frames'] for r in records),
        'host_only':True,'hardware_output_validated':False},indent=2)+'\n')


if __name__=='__main__': main()
