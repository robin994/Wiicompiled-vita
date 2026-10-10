#!/usr/bin/env python3
"""Compile static Wii assets on the computer and append them to one PSARC.

No extracted tree is used at runtime. Original guest files, the DVD index and
executables remain byte-identical; native records are exact-content sidecars.
"""
import argparse,collections,hashlib,json,os,struct,subprocess,tempfile,time
from pathlib import Path
from native_assets.formats import leaves,textures,geometry_requests,sections,brstm_blocks,rwav_blocks
from vita_psarc import parse_psarc,iter_entry,create_psarc,verify_psarc
from prepare_mkw_psarc import EXPECTED_EXECUTABLES
from native_assets.shader_cache import import_cache

def sha_file(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()
def fnv(data):
    value=14695981039346656037
    for byte in data:value=((value^byte)*1099511628211)&0xffffffffffffffff
    return value

def prepare(source,work,compiler,audio_compiler,pack=True,shader_cache=None,geometry_minimum=48):
    if not 3<=geometry_minimum<=65535:raise ValueError('geometry minimum must be between 3 and 65535')
    root=work/'payload';root.mkdir(exist_ok=True);requests=work/'requests';requests.mkdir(exist_ok=True)
    audio=work/'audio-requests';audio.mkdir(exist_ok=True)
    inventory=[];fallback=[];known=set();audio_known={};original={};counts=collections.Counter()
    archive=parse_psarc(source)
    def request(data):
        digest=hashlib.sha256(data).hexdigest()
        if digest not in known:
            (requests/(digest+'.avrq')).write_bytes(data);known.add(digest)
        return digest
    def audio_request(meta,data):
        digest=hashlib.sha256(data).hexdigest()
        if digest not in audio_known:
            (audio/(digest+'.awq')).write_bytes(data)
            key=fnv(data[52:60]+data[16:48]+data[48:52]);audio_known[digest]=key
        counts['audio_occurrences']+=1
        inventory.append({**meta,'kind':'audio','request_sha256':digest})
    with source.open('rb') as f:
        for i,name in enumerate(archive.paths,1):
            if not name.startswith('disc/'):raise ValueError('input already contains native sidecars; use AVC base')
            dest=root/name;dest.parent.mkdir(parents=True,exist_ok=True)
            with dest.open('wb') as out:
                for chunk in iter_entry(f,archive,i):out.write(chunk)
            original[name]=sha_file(dest)
            suffix=dest.suffix.lower()
            if suffix in ('.thp','.dol','.rel','.bin','.rkg','.csv','.txt','.dat'):continue
            data=dest.read_bytes()
            if suffix=='.brstm':
                try:
                    for meta,req in brstm_blocks(data,name):audio_request(meta,req)
                    counts['brstm_files']+=1
                    head=struct.unpack_from('>I',data,16)[0];info=head+8+struct.unpack_from('>I',data,head+12)[0]
                    if data[info] in (0,1):counts['brstm_already_pcm']+=1
                except (ValueError,struct.error) as error:fallback.append({'path':name,'kind':'audio','reason':str(error)})
                continue
            if suffix=='.brsar':
                try:
                    wave_count=0
                    for meta,req in rwav_blocks(data,name):
                        audio_request(meta,req);wave_count+=1
                    counts['brsar_files']+=1
                    if not wave_count:
                        counts['brsar_without_rwav']+=1
                        fallback.append({'path':name,'kind':'audio','reason':'legacy bank has no supported RWAV; original AX decoder retained'})
                except (ValueError,struct.error) as error:fallback.append({'path':name,'kind':'audio','reason':str(error)})
                continue
            try:
                for leaf,data in leaves(data,name):
                    for tex in textures(data,leaf):
                        counts['texture_occurrences']+=1
                        ids=[request(req) for req in tex.requests()]
                        inventory.append({'kind':'texture','source':tex.name,'width':tex.width,'height':tex.height,
                            'gx_format':tex.gx_format,'mips':tex.levels,'requests':ids})
                    for _,model,offset in sections(data):
                        if data[offset:offset+4]!=b'MDL0':continue
                        counts['model_occurrences']+=1
                        try:
                            ids=[request(req) for _,req in geometry_requests(data,leaf,only_model=offset,minimum_count=geometry_minimum)]
                            counts['geometry_draw_occurrences']+=len(ids)
                            inventory.append({'kind':'model','source':leaf+'!'+model,'requests':ids})
                        except (ValueError,struct.error) as error:
                            fallback.append({'path':leaf+'!'+model,'kind':'model','reason':str(error)})
            except (ValueError,struct.error) as error:fallback.append({'path':name,'kind':'resource','reason':str(error)})
            if i%100==0:print(json.dumps({'scanned':i,**counts,'unique_graphics_requests':len(known),'unique_audio_requests':len(audio_known)}),flush=True)
    for name,expected in EXPECTED_EXECUTABLES.items():
        if original['disc/'+name]!=expected:raise ValueError('executable differs from translated RMCP01: '+name)
    # Match the current GXM profile, which keeps the optional BC1 path off.
    subprocess.run([str(compiler.resolve()),str(requests),str(root),'--no-bc1'],check=True)
    subprocess.run([str(audio_compiler.resolve()),str(audio),str(root)],check=True)
    graphics=[];formats=collections.Counter()
    for path in (root/'native/v1').glob('*/*.avnr'):
        record=path.read_bytes();magic,version,kind,source_size,payload_size=struct.unpack_from('<5I',record)
        if magic!=0x524e5641 or version!=1 or len(record)!=28+source_size+payload_size:raise ValueError('compiler emitted invalid AVNR')
        graphics.append((kind,int(path.stem,16),len(record)))
        if kind==1:formats[struct.unpack_from('<I',record,28+source_size)[0]]+=1
    if len(graphics)!=len(known):raise ValueError(f'graphics compiler rejected {len(known)-len(graphics)} requests')
    graphics.sort();index=b'WICNAT01'+struct.pack('<II',1,len(graphics))
    for kind,key,size in graphics:index+=struct.pack('<IQI',kind,key,size)
    (root/'native/v1/index.bin').write_bytes(index)
    audio_entries=[];canonical_audio={}
    for path in sorted((root/'native/v1/audio').glob('*.awpc')):
        record=path.read_bytes();raw_bytes=struct.unpack_from('<I',record,8)[0]
        req=b'WICAUR01'+record[8:52+raw_bytes]
        digest=hashlib.sha256(req).hexdigest()
        key=fnv(record[52:60]+record[16:48]+record[48:52])
        dest=path.with_name(digest+'.awpc')
        if digest in canonical_audio:
            if dest.read_bytes()!=record:raise ValueError('audio exact-content collision')
            path.unlink()
        else:
            os.replace(path,dest);canonical_audio[digest]=key
            audio_entries.append((key,len(record),bytes.fromhex(digest)))
    audio_entries.sort();index=bytearray(b'WICAIDX1'+struct.pack('<II',1,len(audio_entries)))
    for key,size,digest in audio_entries:index+=struct.pack('<QI',key,size)+digest
    (root/'native/v1/audio-index.bin').write_bytes(index)
    stats={**counts,'unique_graphics_records':len(graphics),'native_texture_formats':dict(formats),
        'unique_audio_records':len(audio_entries),'fallback_count':len(fallback),
        'graphics_sidecar_bytes':sum(s for _,_,s in graphics),'audio_sidecar_bytes':sum(s for _,s,_ in audio_entries)}
    report={'format':'wic-native-assets-v1','source':str(source),'source_sha256':sha_file(source),
        'source_bytes':source.stat().st_size,'hardware_validated':False,'runtime_iso_required':False,
        'runtime_extracted_files':False,'original_assets_unchanged':len(original),
        'requires_runtime_option':'WIC_PRECOMPILED_ASSETS=ON','statistics':stats,'fallbacks':fallback,
        'texture_profile':'exact native compact formats or RGBA8; BC1 disabled',
        'geometry_minimum_vertices':geometry_minimum,
        'assets':inventory,'original_sha256':original}
    (work/'inventory.json').write_text(json.dumps(report,indent=2)+'\n')
    if shader_cache:
        report['native_shader_cache']=import_cache(shader_cache,root)
        (work/'inventory.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'phase':'compiled',**stats}),flush=True)
    if pack:
        candidate=work/'native.psarc';report['create']=create_psarc(candidate,[root/'disc',root/'native'],compress_prefixes=('native/v1/audio/','native/v1/textures/','native/v1/geometry/'))
        report['verify']=verify_psarc(candidate,full=True)
        # A changed original asset is a packaging failure, even if conversions
        # succeeded. Verify every original through the actual new archive.
        parsed=parse_psarc(candidate)
        with candidate.open('rb') as f:
            for i,name in enumerate(parsed.paths,1):
                if name not in original:continue
                h=hashlib.sha256()
                for chunk in iter_entry(f,parsed,i):h.update(chunk)
                if h.hexdigest()!=original[name]:raise ValueError('original asset changed: '+name)
        report['output_bytes']=candidate.stat().st_size;report['output_sha256']=sha_file(candidate)
    return report

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=Path('build/vita-data/mariokart-avc.psarc'))
    p.add_argument('--output',type=Path,default=Path('build/vita-data/mariokart-native.psarc'))
    p.add_argument('--work',type=Path,default=None)
    p.add_argument('--compiler',type=Path,default=Path('build/aurora-host/aurora_vita_compile_native_assets'))
    p.add_argument('--audio-compiler',type=Path,default=Path('build/wic-compile-audio'))
    p.add_argument('--no-pack',action='store_true')
    p.add_argument('--shader-cache',type=Path,help='Validated Vita GXP cache records to include as native sidecars')
    p.add_argument('--geometry-min-vertices',type=int,default=48,
                   help='Include smaller immutable draws, matching [vita].geometry_min_vertices (3..65535)')
    args=p.parse_args();source=args.source.resolve();out=args.output.resolve()
    if out.exists() or out.with_suffix(out.suffix+'.json').exists():raise ValueError('output must be new')
    if not args.work:args.work=out.parent/('native-work-'+time.strftime('%Y%m%dT%H%M%SZ',time.gmtime()))
    work=args.work.resolve();work.mkdir(parents=True,exist_ok=False)
    report=prepare(source,work,args.compiler,args.audio_compiler,not args.no_pack,args.shader_cache,args.geometry_min_vertices)
    if not args.no_pack:
        report['output']=str(out);out.parent.mkdir(parents=True,exist_ok=True)
        os.replace(work/'native.psarc',out)
        out.with_suffix(out.suffix+'.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'work':str(work),'output':str(out),**report['statistics']}),flush=True)
if __name__=='__main__':main()
