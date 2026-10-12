#!/usr/bin/env python3
import hashlib,struct,subprocess,sys,tempfile
from pathlib import Path
from vita_psarc import create_psarc
from prepare_mkw_native_assets import fnv
from native_assets.formats import yaz0,leaves,textures,Texture,mip_size

def main():
    binary,compiler=map(Path,sys.argv[1:3])
    # Truncated and invalid compressed resources must fail before generation.
    for b in [b'Yaz0'+struct.pack('>I',2)+bytes(8)+b'\0\0\0',b'Yaz0'+struct.pack('>I',0xffffffff)+bytes(8)]:
        try:yaz0(b)
        except (ValueError,struct.error):pass
        else:raise AssertionError('invalid Yaz0 accepted')
    assert yaz0(b'Yaz0'+struct.pack('>I',3)+bytes(8)+b'\xe0abc')==b'abc'
    tex=Texture('fixture',8,8,0,1,bytes(32));req=next(tex.requests());assert len(req)==68
    with tempfile.TemporaryDirectory(prefix='wic-native-contract-') as tmp:
        work=Path(tmp);root=work/'payload';native=root/'native/v1';(native/'textures').mkdir(parents=True)
        graphics=b'AVNR'+bytes(60);g=native/'textures/000000000000002a.avnr';g.write_bytes(graphics)
        graphics_b=b'AVNR'+bytes(92)
        (native/'textures/000000000000002c.avnr').write_bytes(graphics_b)
        (native/'index.bin').write_bytes(b'WICNAT01'+struct.pack('<II',1,2)+
            struct.pack('<IQI',1,42,len(graphics))+
            struct.pack('<IQI',1,44,len(graphics_b)))
        shaders=native/'shaders/gxm-cg-gxp-v1';shaders.mkdir(parents=True)
        # The transport remains opaque. GXP ABI/hash validation is exercised
        # separately by Aurora's cache tests and the native device consumer.
        (shaders/'f-000000000000002a.gxp').write_bytes(graphics)
        requests=work/'requests';requests.mkdir()
        raw=bytearray()
        # Cross two 1024-frame native block boundaries, including AX tasks which
        # end in the middle of a DSP frame and of a prepared block.
        frames=2050
        for i in range(frames):raw+=bytes([(i%8)<<4|(i%16)])+bytes((i*11+n*23)&255 for n in range(7))
        coefficients=[1200,-300,900,600,1800,-500,1100,100,700,-700,1000,300,1500,-300,800,20]
        request=b'WICAUR01'+struct.pack('<II16h2h',len(raw),frames*14,*coefficients,150,-210)+raw
        q=requests/'fixture.awq';q.write_bytes(request)
        subprocess.run([str(compiler.resolve()),str(requests),str(root)],check=True)
        entries=[]
        for record in list((native/'audio').glob('*.awpc')):
            b=record.read_bytes();source_bytes=struct.unpack_from('<I',b,8)[0]
            digest=hashlib.sha256(b'WICAUR01'+b[8:52+source_bytes]).hexdigest()
            record.rename(record.with_name(digest+'.awpc'))
            key=fnv(b[52:60]+b[16:48]+b[48:52])
            entries.append((key,len(b),bytes.fromhex(digest)))
        entries.sort()
        (native/'audio-index.bin').write_bytes(b'WICAIDX1'+struct.pack('<II',1,len(entries))+b''.join(struct.pack('<QI',k,s)+d for k,s,d in entries))
        packed=work/'native.psarc';create_psarc(packed,[root/'native'],compress_prefixes=('native/v1/audio/',))
        bare=work/'bare';(bare/'disc').mkdir(parents=True);(bare/'disc/placeholder').write_bytes(b'original')
        fallback=work/'bare.psarc';create_psarc(fallback,[bare/'disc'])
        subprocess.run([str(binary.resolve()),str(packed),str(g),str(q),str(fallback)],check=True)
if __name__=='__main__':main()
