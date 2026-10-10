#!/usr/bin/env python3
"""Bind the native asset consumer audit to an unstripped ARM candidate."""
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
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('elf',type=Path)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    with args.elf.open('rb') as stream:
        elf=ELFFile(stream)
        assert elf.elfclass==32 and elf.little_endian and elf['e_machine']=='EM_ARM'
        symbols=list(elf.get_section_by_name('.symtab').iter_symbols())
        names={s.name:s for s in symbols}
        loads=[(s['p_vaddr'],s.data()) for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
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
    def targets(symbol):
        start=symbol['st_value']&~1
        code=subprocess.check_output([str(objdump),'-d','--start-address='+hex(start),
            '--stop-address='+hex(start+symbol['st_size']),str(args.elf)],text=True)
        return {destination(int(a,16)) for a in re.findall(r'\b(?:blx?|b(?:\.w)?)\s+([0-9a-f]+)\s+<',code)}
    required=['WicReadNativeAsset','WicNativeAudioFrame','WicReportNativeAudio']
    for name in required:
        s=names[name]
        assert s['st_shndx']!='SHN_UNDEF' and s['st_info']['bind']=='STB_GLOBAL' and s['st_size'],name
    initializers={}
    for token in ['assets10InitializeE','assets15InitializeAudioEv']:
        matching=[s for s in symbols if token in s.name and s['st_size'] and s.name.startswith('_ZN') and 'veneer' not in s.name]
        assert len(matching)==1,(token,[s.name for s in matching])
        initializers[matching[0].name]=matching[0]
    main_targets=targets(names['main'])
    assert all(s['st_value']&~1 in main_targets for s in initializers.values()),'catalogues not initialized by game main'
    audio_target=names['WicNativeAudioFrame']['st_value']&~1
    consumers=[s for s in symbols if s['st_size'] and s['st_info']['type']=='STT_FUNC'
        and ('ReadSample' in s.name or 'Resample' in s.name or 'ProcessVoice' in s.name)
        and 'veneer' not in s.name]
    audio_calls=[s.name for s in consumers if audio_target in targets(s)]
    assert audio_calls,'native PCM callback not called by the linked AX path'
    geometry=[s for s in symbols if 'prepare_streamed_draw_into' in s.name and s['st_size'] and 'veneer' not in s.name]
    # GCC can inline the transform helper into the streaming path. The native
    # loader is the cross-translation-unit call that must remain either way.
    native_geometry=[s for s in symbols if 'load_native_geometry' in s.name and s['st_size'] and 'veneer' not in s.name]
    assert geometry and native_geometry
    geometry_target=native_geometry[0]['st_value']&~1
    called=any(geometry_target in targets(s) for s in geometry)
    helpers=[s for s in symbols if 'prepare_native_geometry' in s.name and s['st_size'] and 'veneer' not in s.name]
    if not called:
        called=any(h['st_value']&~1 in targets(s) and geometry_target in targets(h) for s in geometry for h in helpers)
    assert called,'streamed draw does not use native geometry'
    texture=[s for s in symbols if 'load_native_texture' in s.name and s['st_size'] and 'veneer' not in s.name]
    texture_creation=[s for s in symbols if 'get_or_upload' in s.name and s['st_size'] and 'TextureCache' in s.name and 'veneer' not in s.name]
    assert texture and any(texture[0]['st_value']&~1 in targets(s) for s in texture_creation),'GXM texture creation not bound to native records'
    result={'passed':True,'elf_sha256':hashlib.sha256(args.elf.read_bytes()).hexdigest(),
        'required_global_consumers':{n:hex(names[n]['st_value']) for n in required},
        'main_calls_both_catalogue_initializers':True,'ax_native_pcm_calls':audio_calls,
        'streamed_draw_calls_native_geometry':True,'gxm_texture_creation_calls_native_reader':True,
        'scope':'Static ARM call-site and symbol audit. No device cache-hit, rendering or FPS proof.'}
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
