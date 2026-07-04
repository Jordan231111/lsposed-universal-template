#!/usr/bin/env python3
"""ELF-aware ARM64 analyzer for the target .so (set RE_SO=/path/to/lib.so).
Handles proper VA<->file-offset mapping via program headers, resolves
ADRP+ADD/LDR page targets, reads vtable pointers, and disassembles.
"""
import struct, sys, capstone

SO = __import__('os').environ.get('RE_SO','')
data = open(SO, 'rb').read()

# ---- parse ELF64 program headers for VA<->offset mapping ----
assert data[:4] == b'\x7fELF'
e_phoff = struct.unpack_from('<Q', data, 0x20)[0]
e_phentsize = struct.unpack_from('<H', data, 0x36)[0]
e_phnum = struct.unpack_from('<H', data, 0x38)[0]
segs = []  # (vaddr, off, filesz, memsz, flags)
for i in range(e_phnum):
    base = e_phoff + i * e_phentsize
    p_type = struct.unpack_from('<I', data, base)[0]
    if p_type != 1:  # PT_LOAD
        continue
    p_flags = struct.unpack_from('<I', data, base + 4)[0]
    p_off = struct.unpack_from('<Q', data, base + 8)[0]
    p_vaddr = struct.unpack_from('<Q', data, base + 16)[0]
    p_filesz = struct.unpack_from('<Q', data, base + 32)[0]
    p_memsz = struct.unpack_from('<Q', data, base + 40)[0]
    segs.append((p_vaddr, p_off, p_filesz, p_memsz, p_flags))

def va_to_off(va):
    for vaddr, off, filesz, memsz, flags in segs:
        if vaddr <= va < vaddr + memsz:
            if va - vaddr < filesz:
                return off + (va - vaddr)
            return None  # in .bss (no file backing)
    return None

def off_to_va(off):
    for vaddr, o, filesz, memsz, flags in segs:
        if o <= off < o + filesz:
            return vaddr + (off - o)
    return None

def read_u32_va(va):
    o = va_to_off(va)
    return struct.unpack_from('<I', data, o)[0] if o is not None else None

def read_u64_va(va):
    o = va_to_off(va)
    return struct.unpack_from('<Q', data, o)[0] if o is not None else None

def read_cstr_va(va, maxlen=80):
    o = va_to_off(va)
    if o is None: return None
    s = b''
    while o < len(data) and data[o] != 0 and len(s) < maxlen:
        s += data[o:o+1]; o += 1
    return s.decode('latin1', 'replace')

md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_LITTLE_ENDIAN)
md.detail = True

def disas(start_va, count=120, stop_at_ret=True):
    o = va_to_off(start_va)
    if o is None:
        print("  <no file backing for 0x%X>" % start_va); return
    buf = data[o:o + count * 4]
    adrp = {}
    for insn in md.disasm(buf, start_va):
        m, op = insn.mnemonic, insn.op_str
        ann = ""
        if m == 'adrp':
            try:
                reg = insn.reg_name(insn.operands[0].reg)
                page = insn.operands[1].imm
                adrp[reg] = page
                ann = "  ; page=0x%X" % page
            except Exception: pass
        elif m == 'add' and len(insn.operands) == 3 and insn.operands[2].type == capstone.arm64.ARM64_OP_IMM:
            rn = insn.reg_name(insn.operands[1].reg)
            if rn in adrp:
                va = adrp[rn] + insn.operands[2].imm
                rd = insn.reg_name(insn.operands[0].reg)
                adrp[rd] = None
                s = read_cstr_va(va)
                printable = s if s and all(32 <= ord(c) < 127 for c in s) else None
                ann = '  ; = 0x%X%s' % (va, ('  "%s"' % printable) if printable else '')
                adrp[rd] = va  # remember resolved address in reg
        elif m == 'ldr' and len(insn.operands) == 2 and insn.operands[1].type == capstone.arm64.ARM64_OP_MEM:
            base_reg = insn.reg_name(insn.operands[1].mem.base)
            disp = insn.operands[1].mem.disp
            if base_reg in adrp and isinstance(adrp.get(base_reg), int):
                va = adrp[base_reg] + disp
                val = read_u64_va(va)
                ann = '  ; [0x%X] = 0x%X' % (va, val if val else 0)
        elif m == 'bl':
            try: ann = '  ; call 0x%X' % insn.operands[0].imm
            except Exception: pass
        elif m in ('cbz','cbnz','tbz','tbnz') or m.startswith('b.') or m == 'b':
            ann += '   <<<BR'
        print('0x%08X: %08X  %-7s %s%s' % (insn.address, read_u32_va(insn.address) or 0, m, op, ann))
        if stop_at_ret and m == 'ret':
            break

if __name__ == '__main__':
    print("== LOAD segments ==")
    for vaddr, off, filesz, memsz, flags in segs:
        fl = ''.join(c for c,b in zip('RWX',(4,2,1)) if flags & b)
        print("  VA 0x%08X off 0x%08X filesz 0x%08X memsz 0x%08X [%s]" % (vaddr, off, filesz, memsz, fl))
    print()
    cmd = sys.argv[1]
    if cmd == 'dis':
        disas(int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 120)
    elif cmd == 'vtbl':  # read N pointers from a vtable VA
        base = int(sys.argv[2], 16); n = int(sys.argv[3]) if len(sys.argv) > 3 else 16
        for i in range(n):
            p = read_u64_va(base + i * 8)
            print("  [+0x%02X] (idx %2d) -> 0x%X" % (i*8, i, p if p else 0))
    elif cmd == 'u64':
        print("0x%X" % (read_u64_va(int(sys.argv[2],16)) or 0))
    elif cmd == 'off':
        print("va 0x%X -> off %r" % (int(sys.argv[2],16), va_to_off(int(sys.argv[2],16))))
