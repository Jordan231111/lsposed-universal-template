import struct,sys
data=open(__import__('os').environ.get('RE_SO',''),'rb').read()
ea=int(sys.argv[1],16); a=ea
for _ in range(0x4000):
    a-=4
    i=struct.unpack_from('<I',data,a)[0]
    if (i&0xFFC003FF)==0xA98003FD or (i&0xFF8003FF)==0xD10003FF:
        print("func start ~0x%X"%a); break
