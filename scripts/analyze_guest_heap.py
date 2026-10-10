#!/usr/bin/env python3
"""Inspect a preserved Wii ExpHeap; never modifies device/dump memory."""
import argparse
import json
from pathlib import Path
import struct


def analyze(data:bytes,base:int,request:int=216,alignment:int=4):
    if len(data)<80:raise ValueError('truncated ExpHeap header')
    def word(address):
        if address<base or address+4>base+len(data):raise ValueError('word outside heap dump')
        return struct.unpack_from('>I',data,address-base)[0]
    if word(base)!=0x45585048:raise ValueError('not an EXPH heap')
    start,end=word(base+24),word(base+28)
    if start<base+80 or end!=base+len(data) or start>end:raise ValueError('heap extent does not match dump')
    if request<0 or request>0xfffffffc or alignment==0 or abs(alignment)&(abs(alignment)-1):raise ValueError('invalid allocation request/alignment')
    issues=[];lists={};extents=[];rounded=(max(request,1)+3)&~3
    def issue(kind,**fields):
        if len(issues)<100:issues.append({'kind':kind,**fields})
    for kind,head,tail,tag in [('free',60,64,0x4652),('used',68,72,0x5544)]:
        node=word(base+head);previous=0;seen=set();nodes=[]
        while node:
            if node in seen:issue('cycle',list=kind,address=hex(node));break
            if len(seen)>=100000:issue('traversal_limit',list=kind);break
            if node<start or node+16>end or node%4:issue('node_outside_heap',list=kind,address=hex(node));break
            seen.add(node)
            magic,size,prev,next_node=(word(node+i) for i in (0,4,8,12))
            extent_valid=node+16+size<=end
            record={'address':hex(node),'tag':hex(magic),'bytes':size,'prev':hex(prev),'next':hex(next_node),'extent_valid':extent_valid}
            if magic>>16!=tag:issue('block_tag',list=kind,address=hex(node),tag=hex(magic))
            if prev!=previous:issue('backlink',list=kind,address=hex(node),expected=hex(previous),actual=hex(prev))
            if not extent_valid:issue('block_extent',list=kind,address=hex(node),bytes=size)
            else:
                extents.append((node,node+16+size,kind))
                a=abs(alignment)
                if alignment>0:aligned=(node+16+a-1)&~(a-1);fits=aligned+rounded<=node+16+size
                else:aligned=(node+16+size-rounded)&~(a-1);fits=aligned>=node+16
                record['request_fits']=kind=='free' and fits
                if record['request_fits']:record['candidate_payload']=hex(aligned)
            nodes.append(record);previous=node;node=next_node
        if previous!=word(base+tail):issue('tail',list=kind,expected=hex(word(base+tail)),actual=hex(previous))
        lists[kind]={'nodes':nodes,'payload_bytes':sum(n['bytes'] for n in nodes if n['extent_valid']),
                     'largest_payload':max((n['bytes'] for n in nodes if n['extent_valid']),default=0)}
    extents.sort()
    for before,after in zip(extents,extents[1:]):
        if before[1]>after[0]:issue('overlap',first=hex(before[0]),second=hex(after[0]))
    return {'base':hex(base),'start':hex(start),'end':hex(end),'dump_bytes':len(data),
            'request':request,'alignment':alignment,'lists':lists,'issues':issues,'integrity_valid':not issues,
            'fitting_free_blocks':sum(bool(n.get('request_fits')) for n in lists['free']['nodes']),
            'interpretation':'corrupt heap metadata' if issues else 'bounded allocation fit inspection',
            'device_or_dump_writes':False}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump',type=Path);parser.add_argument('--base',type=lambda x:int(x,0),required=True)
    parser.add_argument('--request',type=int,default=216);parser.add_argument('--alignment',type=int,default=4)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    args.output.write_text(json.dumps(analyze(args.dump.read_bytes(),args.base,args.request,args.alignment),indent=2)+'\n')


if __name__=='__main__':main()
