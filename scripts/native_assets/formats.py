"""Bounded readers for Wii resource containers; conversion runs on the host.

Field layouts were checked against RiiStudio librii g3d TextureIO/ModelIO and
vgmstream src/meta/brstm.c. All original guest structures remain in the PSARC.
"""
import struct
from dataclasses import dataclass

LIMIT = 64 * 1024 * 1024
GX_FORMAT = {**{n:n for n in range(7)}, 8:7, 9:8, 10:9, 14:10}
TILES = {0:(8,8,32),1:(8,4,32),2:(8,4,32),3:(4,4,32),4:(4,4,32),
         5:(4,4,32),6:(4,4,64),8:(8,8,32),9:(8,4,32),10:(4,4,32),14:(8,8,32)}

def u16(b,o): return struct.unpack_from('>H',b,o)[0]
def u32(b,o): return struct.unpack_from('>I',b,o)[0]
def s32(b,o): return struct.unpack_from('>i',b,o)[0]
def extent(b,o,n):
    if o<0 or n<0 or o>len(b) or n>len(b)-o: raise ValueError('resource extent outside file')
    return b[o:o+n]
def cstring(b,o):
    if o<0 or o>=len(b): raise ValueError('string outside file')
    end=b.find(b'\0',o,min(len(b),o+4096))
    if end<0: raise ValueError('unterminated resource name')
    return b[o:end].decode('utf-8',errors='strict')
def mip_size(w,h,fmt,levels):
    if fmt not in TILES or not 0<w<=4096 or not 0<h<=4096 or not 1<=levels<=13:
        raise ValueError('unsupported texture dimensions/format/mip count')
    tw,th,sz=TILES[fmt]
    return sum(((max(1,w>>i)+tw-1)//tw)*((max(1,h>>i)+th-1)//th)*sz for i in range(levels))

def yaz0(b):
    if b[:4]!=b'Yaz0': return b
    size=u32(b,4)
    if size>LIMIT: raise ValueError('Yaz0 output exceeds limit')
    out=bytearray();pos=16
    while len(out)<size:
        flags=extent(b,pos,1)[0];pos+=1
        for shift in range(7,-1,-1):
            if len(out)==size: break
            if flags&(1<<shift):out+=extent(b,pos,1);pos+=1
            else:
                x,y=extent(b,pos,2);pos+=2;distance=((x&15)<<8|y)+1
                count=x>>4
                if not count:count=extent(b,pos,1)[0]+18;pos+=1
                else:count+=2
                if distance>len(out) or count>size-len(out): raise ValueError('invalid Yaz0 back-reference')
                if distance==1: out.extend(out[-1:] * count)
                elif count<=distance:out.extend(out[len(out)-distance:len(out)-distance+count])
                else:
                    pattern=out[-distance:];out.extend((pattern*((count+distance-1)//distance))[:count])
    return bytes(out)

def leaves(b,name,depth=0):
    if depth>8: raise ValueError('nested resource depth exceeds limit')
    b=yaz0(b)
    if b[:4]==b'\x55\xaa\x38\x2d':
        root=u32(b,4);extent(b,root,12);count=u32(b,root+8)
        if not 1<=count<=100000: raise ValueError('invalid U8 node count')
        strings=root+count*12;extent(b,root,count*12)
        stack=[('',count)]
        for i in range(1,count):
            while stack and i>=stack[-1][1]:stack.pop()
            if not stack: raise ValueError('invalid U8 directory boundary')
            node=root+i*12;word=u32(b,node);local=cstring(b,strings+(word&0xffffff))
            if '/' in local or local=='..' or (local=='.' and not word>>24):raise ValueError('invalid U8 component')
            if local=='.':local=''
            local=stack[-1][0]+local
            if word>>24:
                end=u32(b,node+8)
                if not i<end<=stack[-1][1]:raise ValueError('invalid U8 directory range')
                stack.append((local+'/',end))
            else:
                child=extent(b,u32(b,node+4),u32(b,node+8))
                yield from leaves(child,name+'!'+local,depth+1)
    else:yield name,b

def dictionary(b,base):
    if not base:return []
    size,count=u32(b,base),u32(b,base+4)
    if count>100000 or size<24+count*16:raise ValueError('invalid BRRES dictionary')
    extent(b,base,size);result=[]
    for i in range(count):
        node=base+24+i*16
        name=cstring(b,base+s32(b,node+8));target=base+s32(b,node+12)
        extent(b,target,4);result.append((name,target))
    return result

def sections(b):
    if b[:4]!=b'bres': return []
    if u16(b,4)!=0xfeff or u32(b,8)>len(b):raise ValueError('invalid BRRES header')
    root=u16(b,12)
    if extent(b,root,4)!=b'root':raise ValueError('missing BRRES root')
    result=[]
    for group,d in dictionary(b,root+8):
        for name,o in dictionary(b,d):result.append((group,name,o))
    return result

@dataclass
class Texture:
    name:str; width:int; height:int; gx_format:int; levels:int; pixels:bytes
    palette_format:int=0; palette:bytes=b''
    def requests(self):
        # Some game paths bind only a prefix of the mip chain. Preparing each
        # prefix avoids making the offline cache depend on the chosen GX LOD.
        for levels in range(1,self.levels+1):
            pixels=self.pixels[:mip_size(self.width,self.height,self.gx_format,levels)]
            yield struct.pack('<9I',1,self.width,self.height,GX_FORMAT[self.gx_format],
                              self.palette_format,levels,0,len(pixels),len(self.palette))+pixels+self.palette

def textures(b,name):
    if b[:4]==b'bres':
        parts=sections(b);palettes={}
        for _,n,o in parts:
            if b[o:o+4]!=b'PLT0':continue
            size=u32(b,o+4);extent(b,o,size)
            fmt=u32(b,o+24);count=u16(b,o+28)
            if fmt>2 or count>16384:raise ValueError('invalid PLT0')
            palettes[n]=(fmt+1,extent(b,o+u32(b,o+16),count*2))
        for _,n,o in parts:
            if b[o:o+4]!=b'TEX0':continue
            revision=u32(b,o+8)
            if revision not in (1,2,3):raise ValueError('unsupported TEX0 revision')
            shift=4 if revision==2 else 0
            w,h=u16(b,o+28+shift),u16(b,o+30+shift);fmt=u32(b,o+32+shift);levels=u32(b,o+36+shift)
            px=extent(b,o+u32(b,o+16+shift),mip_size(w,h,fmt,levels))
            pf,pal=palettes.get(n,(0,b''))
            if fmt in (8,9,10) and not pal:raise ValueError('TEX0 palette missing: '+n)
            yield Texture(name+'!'+n,w,h,fmt,levels,px,pf,pal)
    elif b[:4]==b'\x00\x20\xaf\x30':
        count=u32(b,4);table=u32(b,8)
        if count>100000:raise ValueError('invalid TPL image count')
        extent(b,table,count*8)
        for i in range(count):
            o=u32(b,table+i*8);p=u32(b,table+i*8+4);extent(b,o,36)
            h,w=u16(b,o),u16(b,o+2);fmt=u32(b,o+4)
            levels=1+extent(b,o+34,1)[0]
            px=extent(b,u32(b,o+8),mip_size(w,h,fmt,levels));pf=0;pal=b''
            if p:
                extent(b,p,12);entries=u16(b,p);pf=u32(b,p+4)+1
                if pf>3:raise ValueError('invalid TPL palette format')
                pal=extent(b,u32(b,p+8),entries*2)
            if fmt in (8,9,10) and not pal:raise ValueError('TPL palette missing')
            yield Texture(name+'!'+str(i),w,h,fmt,levels,px,pf,pal)

def geometry_requests(b,name,only_model=None,minimum_count=48):
    """Object-space geometry only: animated matrices are deliberately retained.

    Each request has the same BE stream, array prefix, stride and component
    descriptors passed to Aurora by the DVD/GX bridge. No materials, matrix
    loads, culling, or draw ordering are removed from the guest display list.
    """
    for _,model_name,mdl in sections(b):
        if b[mdl:mdl+4]!=b'MDL0' or (only_model is not None and mdl!=only_model):continue
        revision=u32(b,mdl+8)
        if revision not in (8,9,11):raise ValueError('unsupported MDL0 revision '+str(revision))
        mesh_slot=10 if revision==11 else 8
        buffers=[]
        for slot,kind in [(2,'pos'),(3,'nrm'),(4,'clr'),(5,'uv')]:
            rel=u32(b,mdl+16+slot*4);group=[]
            for _,o in dictionary(b,mdl+rel if rel else 0):
                comp=u32(b,o+20);fmt=u32(b,o+24)
                frac=b[o+28] if kind in ('pos','uv') else 0
                stride=b[o+29] if kind in ('pos','nrm','uv') else b[o+28]
                count=u16(b,o+30)
                if not stride or count>65535:raise ValueError('invalid MDL0 vertex buffer')
                if kind=='nrm':frac=6 if fmt==1 else 14 if fmt==3 else 0
                if kind=='pos':components=comp+2
                elif kind=='uv':components=comp+1
                elif kind=='nrm':components=9 if comp==1 else 3
                else:components=4
                if kind=='clr':
                    if fmt>5:raise ValueError('unsupported vertex color format')
                    component=5+fmt;element=[2,3,4,2,3,4][fmt]
                else:
                    if fmt>4 or components>9:raise ValueError('unsupported vertex component format')
                    component=fmt;element=[1,1,2,2,4][fmt]*components
                data=extent(b,o+s32(b,o+8),(count-1)*stride+element if count else 0)
                group.append((component,components,frac,stride,element,data))
            buffers.append(group)
        rel=u32(b,mdl+16+mesh_slot*4)
        for mesh_name,poly in dictionary(b,mdl+rel if rel else 0):
            handles=[]
            for o in (poly+24,poly+36):
                capacity,size,offset=u32(b,o),u32(b,o+4),s32(b,o+8)
                if size>capacity:raise ValueError('invalid MDL0 display-list size')
                handles.append(extent(b,o+offset,capacity))
            setup,data=handles;cp={};p=10
            while p<len(setup):
                op=setup[p];p+=1
                if op==0:continue
                if op==8:
                    reg=extent(setup,p,1)[0];cp[reg]=u32(setup,p+1);p+=5
                elif op==0x10:
                    size=(u32(setup,p)>>16)+1;p+=4+size*4
                elif op==0x61:p+=4
                else:raise ValueError('unsupported MDL0 setup command '+hex(op))
                if p>len(setup):raise ValueError('truncated MDL0 setup command')
            if 0x50 not in cp or 0x60 not in cp:raise ValueError('MDL0 missing VCD setup')
            lo,hi=cp[0x50],cp[0x60];attrs=[];stride=0
            for i in range(9):
                if lo&(1<<i):attrs.append([i,1,0,1,0,stride,0,0,0,b'',1]);stride+=1
            ids=struct.unpack_from('>12h',b,poly+72)
            for i in range(12):
                source=(lo>>(9+2*i))&3 if i<4 else (hi>>(2*(i-4)))&3
                if not source:continue
                kind=0 if i==0 else 1 if i==1 else 2 if i<4 else 3
                if ids[i]<0 or ids[i]>=len(buffers[kind]):raise ValueError('MDL0 vertex buffer reference outside dictionary')
                component,components,frac,array_stride,element,array=buffers[kind][ids[i]]
                semantic=9 if i==0 else 10 if i==1 else i+11 if i<4 else i+11
                indexbytes=1 if source==2 else 2 if source==3 else element
                nbt=(kind==1 and (components==9 or cp.get(0x70,0)&(1<<31)))
                nbt3=nbt and bool(cp.get(0x70,0)&(1<<31))
                if nbt:
                    scalar_bytes=[1,1,2,2,4][component]
                    for k in range(3):
                        attrs.append([10+k,source,component,3,frac,stride+(k*indexbytes if nbt3 else 0),
                            0 if nbt3 else k*3*scalar_bytes,array_stride,0,array,element])
                    stride+=indexbytes*(3 if nbt3 else 1)
                else:
                    attrs.append([semantic,source,component,components,frac,stride,0,array_stride,0,array,element])
                    stride+=indexbytes
            draws=[];maximum=[-1]*len(attrs);p=0
            while p<len(data):
                op=data[p];p+=1
                if op==0:continue
                if op in (0x20,0x28,0x30,0x38):extent(data,p,4);p+=4;continue
                primitive={0x80:0,0x90:1,0x98:2,0xa0:3}.get(op&0xf8)
                if primitive is None:raise ValueError('unsupported MDL0 primitive command '+hex(op))
                count=u16(data,p);p+=2;raw=extent(data,p,count*stride);p+=len(raw)
                if not count:continue
                draws.append((primitive,count,raw))
                for ai,a in enumerate(attrs):
                    if a[1] not in (2,3):continue
                    idx_size=1 if a[1]==2 else 2
                    for v in range(count):
                        index=int.from_bytes(raw[v*stride+a[5]:v*stride+a[5]+idx_size],'big')
                        maximum[ai]=max(maximum[ai],index)
            for primitive,count,raw in draws:
                if count<minimum_count:continue
                req=bytearray(struct.pack('<7I',2,count,primitive,stride,0,len(attrs),len(raw)))
                for ai,a in enumerate(attrs):
                    semantic,source,component,components,frac,offset,value_offset,array_stride,le,array,element=a
                    max_index=max((maximum[j] for j,x in enumerate(attrs) if x[9] is array),default=maximum[ai])
                    view=extent(array,0,max_index*array_stride+element) if source in (2,3) else b''
                    req+=struct.pack('<10I',semantic,source,component,components,frac,offset,value_offset,array_stride if view else 0,le,len(view))+view
                req+=raw
                yield name+'!'+model_name+'!'+mesh_name,bytes(req)

def brstm_blocks(b,name):
    if b[:4]!=b'RSTM':return
    if u16(b,4)!=0xfeff or u16(b,6)!=0x100 or u32(b,8)!=len(b):raise ValueError('unsupported BRSTM header')
    head=u32(b,16);info=head+8+u32(b,head+12);table=head+8+u32(b,head+28)
    if b[head:head+4]!=b'HEAD':raise ValueError('BRSTM HEAD missing')
    codec,loop,channels=extent(b,info,3)
    if codec in (0,1):return # Already uncompressed PCM; no ADPCM decoder to bypass.
    if codec!=2:raise ValueError('unsupported BRSTM codec')
    if not 1<=channels<=8:raise ValueError('unsupported BRSTM channel count')
    rate=u16(b,info+4);loop_start=u32(b,info+8);total=u32(b,info+12)
    start=u32(b,info+16);blocks=u32(b,info+20);block_size=u32(b,info+24);block_samples=u32(b,info+28)
    last_bytes=u32(b,info+32);last_samples=u32(b,info+36);last_padded=u32(b,info+40)
    if not blocks or block_size>1024*1024 or (blocks-1)*block_samples+last_samples!=total:
        raise ValueError('invalid BRSTM block metadata')
    coefs=[]
    for ch in range(channels):
        channel=head+8+u32(b,table+8+ch*8)
        coefficient=head+8+u32(b,channel+4)
        coefficients=struct.unpack_from('>16h',b,coefficient)
        history=struct.unpack_from('>2h',b,coefficient+36)
        coefs.append((coefficients,history))
    adpc=u32(b,24);extent(b,adpc,8+blocks*channels*4)
    if b[adpc:adpc+4]!=b'ADPC':raise ValueError('BRSTM seek histories missing')
    for block in range(blocks):
        samples=last_samples if block==blocks-1 else block_samples
        bytes_used=(samples+13)//14*8
        stride=last_padded if block==blocks-1 else block_size
        if bytes_used>stride or (block!=blocks-1 and bytes_used!=block_size) or last_bytes>last_padded:
            raise ValueError('BRSTM block/sample extent mismatch')
        for ch in range(channels):
            offset=start+block*block_size*channels+ch*stride
            raw=extent(b,offset,bytes_used)
            history=struct.unpack_from('>2h',b,adpc+8+(block*channels+ch)*4)
            coefficients=coefs[ch][0]
            request=b'WICAUR01'+struct.pack('<II16h2h',bytes_used,samples,*coefficients,*history)+raw
            yield {'source':name,'block':block,'channel':ch,'offset':offset,'samples':samples,
                   'sample_rate':rate,'loop':bool(loop),'loop_start':loop_start,'total_samples':total},request

def rwav_blocks(b,name):
    """Enumerate embedded Wii RWAV waves without interpreting their sequences."""
    pos=0
    while (pos:=b.find(b'RWAV',pos))>=0:
        start=pos;pos+=4
        try:
            if u16(b,start+4)!=0xfeff:continue
            size=u32(b,start+8);wave=extent(b,start,size)
            if size>LIMIT:raise ValueError('RWAV exceeds bounded offline wave size')
            info=u32(wave,16);data=u32(wave,24)
            if wave[info:info+4]!=b'INFO' or wave[data:data+4]!=b'DATA':continue
            codec,loop,channels=extent(wave,info+8,3)
            if codec!=2:continue # PCM sources already avoid ADPCM decoding.
            if not 1<=channels<=8:raise ValueError('invalid RWAV channels')
            rate=u16(wave,info+12);nibbles=u32(wave,info+20)
            samples=(nibbles//16)*14+max(0,(nibbles%16)-2)
            if not samples:continue
            frames=(samples+13)//14;table=info+8+u32(wave,info+24)
            for ch in range(channels):
                channel=info+8+u32(wave,table+ch*4)
                begin=data+8+u32(wave,channel);coef=info+8+u32(wave,channel+4)
                coefficients=struct.unpack_from('>16h',wave,coef);hist=struct.unpack_from('>2h',wave,coef+34)
                raw=extent(wave,begin,frames*8)
                request=b'WICAUR01'+struct.pack('<II16h2h',len(raw),samples,*coefficients,*hist)+raw
                yield {'source':name,'wave_offset':start,'channel':ch,'offset':start+begin,
                       'samples':samples,'sample_rate':rate,'loop':bool(loop),
                       'loop_nibble':u32(wave,info+16)},request
        except (struct.error,ValueError) as error:
            raise ValueError(f'{name}: invalid RWAV at {start}: {error}') from error
