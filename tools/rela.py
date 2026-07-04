import struct,sys
data=open(__import__('os').environ.get('RE_SO',''),'rb').read()
e_shoff=struct.unpack_from('<Q',data,0x28)[0]
e_shentsize=struct.unpack_from('<H',data,0x3a)[0]
e_shnum=struct.unpack_from('<H',data,0x3c)[0]
e_shstrndx=struct.unpack_from('<H',data,0x3e)[0]
def sh(i):
    b=e_shoff+i*e_shentsize
    return dict(name=struct.unpack_from('<I',data,b)[0],off=struct.unpack_from('<Q',data,b+24)[0],
                size=struct.unpack_from('<Q',data,b+32)[0],addr=struct.unpack_from('<Q',data,b+16)[0])
shs=[sh(i) for i in range(e_shnum)]
shstr=shs[e_shstrndx]['off']
def sname(n):
    o=shstr+n;e=data.index(b'\0',o);return data[o:e].decode()
secs={sname(s['name']):s for s in shs}
rd=secs['.rela.dyn']
off=rd['off']; size=rd['size']; n=size//24
# Build dicts lazily via full parse (arrays)
import array
# offset(r_offset) -> addend  for type RELATIVE (1027)
# We'll build a dict addend->offsets and offset->addend
off2add={}
add2off={}
base=off
end=off+size
p=base
while p<end:
    r_offset=struct.unpack_from('<Q',data,p)[0]
    r_info=struct.unpack_from('<Q',data,p+8)[0]
    r_addend=struct.unpack_from('<q',data,p+16)[0]
    typ=r_info & 0xffffffff
    if typ==1027:
        off2add[r_offset]=r_addend
        add2off.setdefault(r_addend,[]).append(r_offset)
    p+=24

if __name__=='__main__':
    cmd=sys.argv[1]
    if cmd=='slot':  # read vtable slot(s): base + i*8
        b=int(sys.argv[2],16); cnt=int(sys.argv[3]) if len(sys.argv)>3 else 16
        for i in range(cnt):
            a=b+i*8
            print("[+0x%02X] idx%2d off=0x%X -> 0x%X"%(i*8,i,a,off2add.get(a,0)))
    elif cmd=='xref':  # who references this addend (points to func)
        t=int(sys.argv[2],16)
        offs=add2off.get(t,[])
        print("addend 0x%X referenced by %d reloc offsets:"%(t,len(offs)))
        for o in offs: print("   at 0x%X"%o)
    elif cmd=='read': # single
        a=int(sys.argv[2],16); print("0x%X -> 0x%X"%(a,off2add.get(a,0)))
    elif cmd=='vtblof': # given a slot offset that references func, find vtable start by scanning back for slot idx0? just print
        pass
