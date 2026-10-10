#!/usr/bin/env python3
"""Decode every converted PSARC access unit with host FFmpeg, not Vita proof."""
import argparse
import hashlib
import json
import mmap
from pathlib import Path
import struct
import subprocess
import tempfile
from convert_mkw_video import VIDEO_HEADER,thp_info,thp_frames,access_units,START
from vita_psarc import parse_psarc,iter_entry


def sps_timing(unit):
    """Read Baseline SPS timing; raw demuxer r_frame_rate may describe ticks."""
    marks=list(START.finditer(unit));sps=None
    for i,m in enumerate(marks):
        if unit[m.end()]&31==7:
            sps=unit[m.end()+1:marks[i+1].start() if i+1<len(marks) else len(unit)];break
    if sps is None:raise ValueError('missing SPS')
    rbsp=bytearray();zeros=0
    for b in sps:
        if zeros>=2 and b==3:zeros=0;continue
        rbsp.append(b);zeros=zeros+1 if b==0 else 0
    pos=0
    def bits(n):
        nonlocal pos
        if pos+n>len(rbsp)*8:raise ValueError('truncated SPS')
        value=0
        for _ in range(n):value=(value<<1)|((rbsp[pos//8]>>(7-pos%8))&1);pos+=1
        return value
    def ue():
        zeros=0
        while not bits(1):
            zeros+=1
            if zeros>31:raise ValueError('invalid SPS Exp-Golomb')
        return (1<<zeros)-1+bits(zeros)
    if bits(8)!=66:raise ValueError('Baseline SPS required')
    bits(8);level=bits(8);ue();ue();poc=ue()
    if poc==0:ue()
    elif poc==1:
        bits(1);ue();ue();cycles=ue()
        if cycles>255:raise ValueError('invalid SPS POC cycle')
        for _ in range(cycles):ue()
    elif poc!=2:raise ValueError('invalid SPS POC type')
    ue();bits(1);w=(ue()+1)*16;h=(ue()+1)*16
    if not bits(1):raise ValueError('interlaced AVC unsupported')
    bits(1)
    if bits(1):
        left,right,top,bottom=(ue() for _ in range(4));w-=2*(left+right);h-=2*(top+bottom)
    if not bits(1):raise ValueError('missing AVC VUI')
    if bits(1):
        if bits(8)==255:bits(16);bits(16)
    if bits(1):bits(1)
    if bits(1):
        bits(3);bits(1)
        if bits(1):bits(8);bits(8);bits(8)
    if bits(1):ue();ue()
    if not bits(1):raise ValueError('missing AVC timing info')
    tick,scale,fixed=bits(32),bits(32),bits(1)
    if not tick or not scale:raise ValueError('invalid AVC clock')
    return {'width':w,'height':h,'level':level,'num_units_in_tick':tick,'time_scale':scale,
            'fixed_frame_rate_flag':bool(fixed),'progressive_frame_rate':scale/(2*tick)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive',type=Path)
    parser.add_argument('--ffprobe',default='ffprobe')
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();archive=parse_psarc(args.archive);results=[]
    with args.archive.open('rb') as source,tempfile.TemporaryDirectory(prefix='wic-avc-verify-') as td:
        work=Path(td)
        for index,name in enumerate(archive.paths,1):
            if not name.lower().endswith('.thp'):continue
            clip=work/'video.thp';stream=work/'video.h264'
            with clip.open('wb') as out:
                for chunk in iter_entry(source,archive,index):out.write(chunk)
            with clip.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data,stream.open('wb') as es:
                info=thp_info(data)
                timing=None
                for _,_,parts in thp_frames(data,info):
                    at,length=parts[0]
                    magic,version,w,h,n,flags=VIDEO_HEADER.unpack_from(data,at)
                    if (magic,version,w,h,flags)!=(b'WAVC',1,info['width'],info['height'],1) or 24+n>length:
                        raise ValueError('invalid AVC component '+name)
                    unit=data[at+24:at+24+n]
                    if len(list(access_units(unit)))!=1:raise ValueError('one IDR required per component')
                    if timing is None:timing=sps_timing(unit)
                    es.write(unit)
            process=subprocess.run([args.ffprobe,'-v','error','-err_detect','explode','-count_frames',
                '-select_streams','v:0','-show_entries',
                'stream=width,height,nb_read_frames,has_b_frames,profile,color_range,level,r_frame_rate',
                '-of','json',str(stream)],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            if process.stderr.strip():raise ValueError('codec decoder errors: '+process.stderr)
            decoded=json.loads(process.stdout)['streams'][0]
            if (decoded['width'],decoded['height'],int(decoded['nb_read_frames']),decoded['has_b_frames'])!=(info['width'],info['height'],info['frames'],0):
                raise ValueError('decoded geometry/count mismatch '+name)
            if decoded['profile']!='Constrained Baseline' or decoded['color_range']!='pc':
                raise ValueError('unsupported profile/range '+name)
            if abs(timing['progressive_frame_rate']-info['fps'])>0.001 or (timing['width'],timing['height'])!=(info['width'],info['height']):
                raise ValueError('SPS geometry/frame rate changed '+name)
            results.append({'path':name,**info,'decoded':decoded,'sps_timing':timing,'full_host_decode':True})
            print(name,decoded['nb_read_frames'],'frames PASS',flush=True)
    h=hashlib.sha256()
    with args.archive.open('rb') as f:
        for chunk in iter(lambda:f.read(4*1024*1024),b''):h.update(chunk)
    args.report.write_text(json.dumps({'archive':str(args.archive.resolve()),'sha256':h.hexdigest(),
        'videos':results,'decoded_frames':sum(v['frames'] for v in results),'host_only':True,
        'vita_hardware_validated':False},indent=2)+'\n')


if __name__=='__main__':main()
