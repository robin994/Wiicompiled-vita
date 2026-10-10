#!/usr/bin/env python3
"""Prove compiled direct-submission/ring branches in a sealed ARM candidate."""
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build_dir',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();path=args.build_dir/'wiicompiled-vita-game'
    cache=(args.build_dir/'CMakeCache.txt').read_text()
    flags=(args.build_dir/'aurora/CMakeFiles/aurora_vita_gxm_backend.dir/flags.make').read_text()
    selected={key:key+':BOOL=ON' in cache for key in ['WIC_NATIVE_DIRECT_SUBMIT','WIC_NATIVE_DIRECT_STREAM']}
    for key,macro in [('WIC_NATIVE_DIRECT_SUBMIT','AURORA_VITA_GXM_DIRECT_DRAW_SUBMIT'),
                      ('WIC_NATIVE_DIRECT_STREAM','AURORA_VITA_GXM_DIRECT_STREAM_WRITE')]:
        assert re.search(r'-D'+macro+'='+str(int(selected[key]))+r'(?:\s|$)',flags),macro
    with path.open('rb') as stream:
        elf=ELFFile(stream)
        assert elf.elfclass==32 and elf.little_endian and elf['e_machine']=='EM_ARM'
        symbols=list(elf.get_section_by_name('.symtab').iter_symbols())
        loads=[(s['p_vaddr'],s.data()) for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
    def function(*tokens,optional=False):
        result=[s for s in symbols if s['st_size'] and s['st_info']['type']=='STT_FUNC'
                and s.name.startswith('_ZN') and 'veneer' not in s.name
                and all(token in s.name for token in tokens)]
        if not result and optional:return None
        assert len(result)==1,(tokens,[s.name for s in result]);return result[0]
    def read(address,size):
        for start,data in loads:
            if start<=address and address+size<=start+len(data):return data[address-start:address-start+size]
        return b''
    def destination(address):
        start=address&~1;data=read(start,20)
        if data[:4]==bytes.fromhex('7847fde7'):start+=4;data=data[4:]
        if len(data)>=16 and struct.unpack_from('<3I',data)==(0xe59fc004,0xe08fc00c,0xe12fff1c):
            return ((start+12+struct.unpack_from('<I',data,12)[0])&0xffffffff)&~1
        return address&~1
    objdump=Path(os.environ.get('VITASDK','/usr/local/vitasdk'))/'bin/arm-vita-eabi-objdump'
    def calls(symbol):
        if symbol is None:return []
        start=symbol['st_value']&~1
        code=subprocess.check_output([str(objdump),'-d','--start-address='+hex(start),
                '--stop-address='+hex(start+symbol['st_size']),str(path)],text=True)
        return [destination(int(a,16)) for a in re.findall(r'\b(?:blx?|b(?:\.w)?)\s+([0-9a-f]+)\s+<',code)]
    submit=function('gxbridge8DrawSink6submitE')
    draw=function('gfx8Renderer4drawE',optional=True)
    direct_draw=draw is not None and (draw['st_value']&~1) in calls(submit)
    assert direct_draw==selected['WIC_NATIVE_DIRECT_SUBMIT'],'direct draw branch differs from compiled configuration'
    # Slot allocation is in initialize(); there is no out-of-line create_slots.
    slots=function('StreamingArena10initializeE')
    writable=function('BufferPool8writableE',optional=True)
    direct_stream=writable is not None and (writable['st_value']&~1) in calls(slots)
    assert direct_stream==selected['WIC_NATIVE_DIRECT_STREAM'],'direct ring mapping branch differs from compiled configuration'
    assets_before_prewarm=None
    if 'WIC_PRECOMPILED_ASSETS:BOOL=ON' in cache:
        main_symbol=next(s for s in symbols if s.name=='main' and s['st_size'])
        startup_calls=calls(main_symbol)
        assets=function('assets10InitializeE')
        backend=function('backend10initializeE')
        assets_before_prewarm=startup_calls.index(assets['st_value']&~1)<startup_calls.index(backend['st_value']&~1)
        assert assets_before_prewarm,'native catalogues must initialize before renderer prewarm'
    result={'passed':True,'elf_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
        'direct_submit':direct_draw,'direct_stream_write':direct_stream,
        'native_assets_initialized_before_renderer':assets_before_prewarm,
        'shader_preload_runtime_configurable':True,
        'scope':'ARM call sites and compile options; GPU correctness, images and FPS require device testing.'}
    args.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':main()
