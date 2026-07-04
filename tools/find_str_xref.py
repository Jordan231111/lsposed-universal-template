#!/usr/bin/env python3
import struct,sys
SO=__import__('os').environ.get('RE_SO','')
data=open(SO,'rb').read()
TEXT_END=0x050A4A40
target=int(sys.argv[1],16)
page=target & ~0xFFF
low=target & 0xFFF
hits=[]
off=0
while off<TEXT_END-4:
    insn=struct.unpack_from('<I',data,off)[0]
    if (insn & 0x9F000000)==0x90000000:  # adrp
        immlo=(insn>>29)&3; immhi=(insn>>5)&0x7FFFF
        imm=(immhi<<2)|immlo
        if imm&(1<<20): imm-=(1<<21)
        p=(off & ~0xFFF)+(imm<<12)
        if p==page:
            rd=insn&0x1F
            for i in range(1,8):
                c=struct.unpack_from('<I',data,off+4*i)[0]
                if (c&0xFF800000)==0x91000000 and ((c>>5)&0x1F)==rd and ((c>>10)&0xFFF)==low:
                    hits.append(off); break
    off+=4
def fstart(ea):
    a=ea
    for _ in range(0x6000):
        a-=4
        i=struct.unpack_from('<I',data,a)[0]
        if (i&0xFFC003FF)==0xA98003FD or (i&0xFF8003FF)==0xD10003FF:
            return a
    return ea
print("xrefs to 0x%X: %d"%(target,len(hits)))
for h in hits: print("  adrp@0x%X func~0x%X"%(h,fstart(h)))
