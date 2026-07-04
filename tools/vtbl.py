#!/usr/bin/env python3
"""Resolve vtable slots via .rela.dyn R_AARCH64_RELATIVE addends (in-place val is 0)."""
import struct, sys
SO=__import__('os').environ.get('RE_SO','')
data=open(SO,'rb').read()
# section headers
e_shoff=struct.unpack_from('<Q',data,0x28)[0]
e_shentsize=struct.unpack_from('<H',data,0x3a)[0]
e_shnum=struct.unpack_from('<H',data,0x3c)[0]
e_shstrndx=struct.unpack_from('<H',data,0x3e)[0]
def sh(i):
    b=e_shoff+i*e_shentsize
    name,typ=struct.unpack_from('<II',data,b)
    flags,addr,off,size=struct.unpack_from('<QQQQ',data,b+8)
    link,info=struct.unpack_from('<II',data,b+0x28)
    entsz=struct.unpack_from('<Q',data,b+0x38)[0]
    return dict(name=name,typ=typ,addr=addr,off=off,size=size,link=link,info=info,entsz=entsz)
shstr=sh(e_shstrndx)
def secname(n):
    o=shstr['off']+n; e=data.index(b'\0',o); return data[o:e].decode()
rela=None
for i in range(e_shnum):
    s=sh(i)
    if secname(s['name'])=='.rela.dyn': rela=s; break
# build map r_offset -> r_addend for RELATIVE (type 1027)
relmap={}
o=rela['off']; end=o+rela['size']
while o<end:
    r_offset,r_info,r_addend=struct.unpack_from('<QQq',data,o)
    if (r_info & 0xffffffff)==1027:
        relmap[r_offset]=r_addend
    o+=24
def slot(vt,idx):
    off=vt+idx*8
    return relmap.get(off)
if __name__=='__main__':
    vt=int(sys.argv[1],16); n=int(sys.argv[2]) if len(sys.argv)>2 else 40
    for i in range(n):
        v=slot(vt,i)
        print("  [+0x%03X] idx %3d -> %s"%(i*8,i,('0x%X'%v) if v else '(0/none)'))
