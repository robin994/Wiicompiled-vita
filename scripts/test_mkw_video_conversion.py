#!/usr/bin/env python3
"""Envelope/index/error contracts; fake encoder here is not codec/device proof."""
import struct
import tempfile
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from convert_mkw_video import thp_info,thp_frames,access_units,convert_clip,patch_index,patch_fst
from analyze_guest_heap import analyze


def rejected(call):
    try: call()
    except (ValueError,struct.error): return
    raise AssertionError("invalid input accepted")


def main():
    heap=bytearray(144);base=0x90010000
    for offset,value in {0:0x45585048,24:base+80,28:base+144,60:base+80,64:base+80,
                         80:0x46520000,84:48}.items():struct.pack_into('>I',heap,offset,value)
    result=analyze(heap,base,16,4);assert result['integrity_valid'] and result['fitting_free_blocks']==1
    assert analyze(heap,base,216,4)['fitting_free_blocks']==0
    struct.pack_into('>I',heap,92,base+80)
    assert any(i['kind']=='cycle' for i in analyze(heap,base)['issues'])
    struct.pack_into('>I',heap,92,base+112);struct.pack_into('>I',heap,84,16)
    struct.pack_into('>I',heap,64,base+112);struct.pack_into('>II',heap,112,0x46520000,16)
    assert any(i['kind']=='backlink' for i in analyze(heap,base)['issues'])
    rejected(lambda:analyze(heap[:-1],base))
    es=b"\0\0\0\1\x09\x10\0\0\1\x07\x42\x00\x20\0\0\1\x08\x80\0\0\1\x05\x80"
    assert len(list(access_units(es*3)))==3
    rejected(lambda:list(access_units(es.replace(b"\x05\x80",b"\x01\x80"))))
    rejected(lambda:list(access_units(es.replace(b"\x07\x42",b"\x07\x64"))))
    with tempfile.TemporaryDirectory() as td:
        work=Path(td); source=work/'input.thp';output=work/'output.thp'
        prefix=bytearray(96)
        struct.pack_into('>4sIIIf7I',prefix,0,b'THP\0',0x11000,224,0,59.94,3,224,3*224,48,0,96,96+2*224)
        struct.pack_into('>I',prefix,48,2); prefix[52:68]=bytes([0,1]+[255]*14)
        struct.pack_into('>III',prefix,68,32,32,0)
        audio=[bytes([i])*64 for i in range(3)]
        records=[]
        for i in range(3):
            record=struct.pack('>IIII',224,224,128,64)+bytes([i+17])*128+audio[i]
            records.append(record+b'\0'*(224-len(record)))
        source.write_bytes(prefix+b''.join(records))
        encoder=work/'encoder'
        encoder.write_text('#!'+sys.executable+'\nfrom pathlib import Path\nimport sys\nPath(sys.argv[-1]).write_bytes('+repr(es*3)+')\n')
        encoder.chmod(0o700)
        result=convert_clip(source,output,str(encoder),18,1)
        assert result['audio_byte_identical'] and result['frames']==3 and result['converted_bytes']<result['source_bytes']
        converted=output.read_bytes();info=thp_info(converted)
        for i,(_,_,parts) in enumerate(thp_frames(converted,info)):
            at,n=parts[1];assert converted[at:at+n]==audio[i]
        assert source.read_bytes()==prefix+b''.join(records)
        rejected(lambda:list(thp_frames(converted[:-1],info)))
        header=struct.pack('<8sIIQQ8s',b'WICIDX01',1,1,100000,100000,b'RMCP01\0\0')
        name=b'/thp/a.thp';idx=header+struct.pack('<QIHH',0x4000,1000,len(name),0)+name
        patched,changed=patch_index(idx,{'disc/thp/a.thp':500})
        assert struct.unpack_from('<I',patched,48)[0]==500 and changed=={0x4000:(1000,500)}
        fst=struct.pack('>III',0x01000000,0,2)+struct.pack('>III',0,0x1000,1000)
        assert struct.unpack_from('>I',patch_fst(fst,changed),20)[0]==500
        rejected(lambda:patch_index(idx,{'disc/thp/a.thp':1001}))
        rejected(lambda:patch_index(idx,{'disc/thp/missing.thp':500}))
        rejected(lambda:patch_fst(fst,{0x4000:(999,500)}))
    print('AVC envelope/audio/loop/FST/index/error contracts PASS; hardware not tested')


if __name__=='__main__':main()
