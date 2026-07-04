// memtool: live process memory instrument for RE (root). "Frida without Frida".
//   scan   <pid> <hexval64> [maxhits]     - find 8-byte-aligned occurrences of a value in rw mem
//   scanr  <pid> <hexval64> <lo> <hi> [max] - same, restricted to [lo,hi) VA range
//   read   <pid> <addr> <len>             - hexdump bytes
//   ptrs   <pid> <addr> <count>           - dump <count> qwords starting at addr
//   write  <pid> <addr> <hexbytes>        - write raw bytes (e.g. a0008052)
//   w32    <pid> <addr> <hexword>         - write a 32-bit LE word
//   w64    <pid> <addr> <hexqword>        - write a 64-bit LE qword
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>

static int mem_fd;

static int read_at(uint64_t addr, void *buf, size_t len) {
    ssize_t n = pread64(mem_fd, buf, len, (off_t)addr);
    return n == (ssize_t)len;
}

typedef struct { uint64_t lo, hi; char perm[8]; char name[192]; } Region;

static int load_regions(int pid, Region *regs, int maxr) {
    char path[64]; snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE *f = fopen(path, "r"); if (!f) return -1;
    char line[512]; int n = 0;
    while (fgets(line, sizeof(line), f) && n < maxr) {
        uint64_t lo, hi; char perm[8] = {0}; char name[192] = {0};
        int c = sscanf(line, "%" SCNx64 "-%" SCNx64 " %7s %*s %*s %*s %191[^\n]", &lo, &hi, perm, name);
        if (c < 3) continue;
        regs[n].lo = lo; regs[n].hi = hi;
        strncpy(regs[n].perm, perm, 7);
        regs[n].name[0] = 0; if (c >= 4) { char *p = name; while (*p==' ') p++; strncpy(regs[n].name, p, 191); }
        n++;
    }
    fclose(f);
    return n;
}

static int scannable(Region *r) {
    if (r->perm[0] != 'r' || r->perm[1] != 'w') return 0;
    // heap-ish: scudo, anon, [heap], blank name. Skip file-backed rw (.data of libs) to cut noise? keep blank+scudo+heap+anon.
    if (r->name[0] == 0) return 1;
    if (strstr(r->name, "scudo")) return 1;
    if (strstr(r->name, "heap")) return 1;
    if (strstr(r->name, "anon:")) return 1;
    if (strstr(r->name, "libc_malloc")) return 1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <cmd> <pid> ...\n", argv[0]); return 2; }
    const char *cmd = argv[1];
    int pid = atoi(argv[2]);
    char path[64]; snprintf(path, sizeof(path), "/proc/%d/mem", pid);
    mem_fd = open(path, O_RDWR);
    if (mem_fd < 0) { perror("open mem"); return 1; }

    if (!strcmp(cmd, "read")) {
        uint64_t addr = strtoull(argv[3], 0, 16); size_t len = strtoul(argv[4], 0, 0);
        unsigned char *b = malloc(len);
        if (!read_at(addr, b, len)) { printf("READ FAIL\n"); return 1; }
        for (size_t i = 0; i < len; i++) { if (i%16==0) printf("\n%012" PRIx64 ": ", addr+i); printf("%02x ", b[i]); }
        printf("\n"); return 0;
    }
    if (!strcmp(cmd, "ptrs")) {
        uint64_t addr = strtoull(argv[3], 0, 16); int cnt = atoi(argv[4]);
        for (int i = 0; i < cnt; i++) {
            uint64_t v = 0; read_at(addr + i*8, &v, 8);
            printf("[+0x%03x] %012" PRIx64 " -> 0x%" PRIx64 "\n", i*8, addr+i*8, v);
        }
        return 0;
    }
    if (!strcmp(cmd, "write") || !strcmp(cmd, "w32") || !strcmp(cmd, "w64")) {
        uint64_t addr = strtoull(argv[3], 0, 16);
        unsigned char buf[256]; size_t len = 0;
        if (!strcmp(cmd, "write")) {
            const char *h = argv[4]; size_t hl = strlen(h);
            for (size_t i = 0; i+1 < hl && len < sizeof(buf); i += 2) { unsigned x; sscanf(h+i, "%2x", &x); buf[len++] = (unsigned char)x; }
        } else if (!strcmp(cmd, "w32")) { uint32_t v = (uint32_t)strtoull(argv[4],0,16); memcpy(buf,&v,4); len=4; }
        else { uint64_t v = strtoull(argv[4],0,16); memcpy(buf,&v,8); len=8; }
        ssize_t n = pwrite64(mem_fd, buf, len, (off_t)addr);
        printf("wrote %zd/%zu bytes at 0x%012" PRIx64 "\n", n, len, addr);
        return n == (ssize_t)len ? 0 : 1;
    }
    if (!strcmp(cmd, "freeze")) {
        // freeze <pid> <modelAddr> <floor> <iters> <sleepUs>
        uint64_t m = strtoull(argv[3], 0, 16);
        int floor = atoi(argv[4]);
        long iters = atol(argv[5]);
        long su = argc > 6 ? atol(argv[6]) : 50000;
        int restores = 0;
        for (long it = 0; it < iters; it++) {
            uint32_t plain = 0; unsigned char key[4] = {0};
            read_at(m + 0x80, &plain, 4);
            read_at(m + 0x78, key, 4);
            if ((int)plain < floor) {
                unsigned char obf[4];
                obf[0] = key[0] ^ (unsigned char)(floor & 0xff);
                obf[1] = key[1] ^ (unsigned char)((floor >> 8) & 0xff);
                obf[2] = key[2] ^ (unsigned char)((floor >> 16) & 0xff);
                obf[3] = key[3] ^ (unsigned char)((floor >> 24) & 0xff);
                pwrite64(mem_fd, obf, 4, (off_t)(m + 0x7c));
                uint32_t v = (uint32_t)floor;
                pwrite64(mem_fd, &v, 4, (off_t)(m + 0x80));
                restores++;
            }
            usleep(su);
        }
        printf("freeze done: %d restores over %ld iters\n", restores, iters);
        return 0;
    }
    if (!strcmp(cmd, "mofreeze")) {
        // mofreeze <pid> <objAddr> <val> <iters> <sleepUs>
        // MonitoredObfuscated<int>: value = shadow0[+0x8] ^ shadow1[+0xC]; redundant plain at +0x10.
        uint64_t m = strtoull(argv[3], 0, 16);
        int val = atoi(argv[4]);
        long iters = atol(argv[5]);
        long su = argc > 6 ? atol(argv[6]) : 100000;
        int restores = 0;
        for (long it = 0; it < iters; it++) {
            uint32_t s0 = 0, s1 = 0, plain = 0;
            read_at(m + 0x8, &s0, 4);
            read_at(m + 0xc, &s1, 4);
            read_at(m + 0x10, &plain, 4);
            int cur = (int)(s0 ^ s1);
            if (cur != val || (int)plain != val) {
                uint32_t ns1 = s0 ^ (uint32_t)val;   // keep shadow0, set shadow1 so xor == val
                uint32_t v = (uint32_t)val;
                pwrite64(mem_fd, &ns1, 4, (off_t)(m + 0xc));
                pwrite64(mem_fd, &v, 4, (off_t)(m + 0x10));
                restores++;
            }
            usleep(su);
        }
        printf("mofreeze done: %d restores over %ld iters\n", restores, iters);
        return 0;
    }
    if (!strcmp(cmd, "modiff")) {
        // modiff <pid> <vtableHex> <waitSec> : snapshot every MonitoredObfuscated<int> value,
        // wait while the user plays, then report which objects DECREASED (that's the moves counter).
        uint64_t vt = strtoull(argv[3], 0, 16);
        int waitSec = atoi(argv[4]);
        static Region regs[8192]; int nr = load_regions(pid, regs, 8192);
        static uint64_t addrs[16384]; static int vals[16384]; int n = 0;
        for (int r = 0; r < nr && n < 16384; r++) {
            if (!scannable(&regs[r])) continue;
            uint64_t lo = regs[r].lo, hi = regs[r].hi;
            if (hi <= lo || hi - lo > 256ULL*1024*1024) continue;
            size_t len = (size_t)(hi - lo);
            unsigned char *buf = malloc(len);
            if (!buf) continue;
            if (pread64(mem_fd, buf, len, (off_t)lo) == (ssize_t)len) {
                for (size_t o = 0; o + 8 <= len && n < 16384; o += 8) {
                    uint64_t v; memcpy(&v, buf+o, 8);
                    if (v == vt) {
                        uint64_t obj = lo + o;
                        uint32_t s0=0, s1=0; read_at(obj+0x8,&s0,4); read_at(obj+0xc,&s1,4);
                        addrs[n] = obj; vals[n] = (int)(s0 ^ s1); n++;
                    }
                }
            }
            free(buf);
        }
        printf("modiff: tracking %d guards; PLAY NOW, waiting %ds...\n", n, waitSec);
        fflush(stdout);
        sleep(waitSec);
        int dec = 0;
        for (int i = 0; i < n; i++) {
            uint32_t s0=0, s1=0; read_at(addrs[i]+0x8,&s0,4); read_at(addrs[i]+0xc,&s1,4);
            int nv = (int)(s0 ^ s1);
            if (nv < vals[i]) { printf("DEC 0x%012" PRIx64 " : %d -> %d\n", addrs[i], vals[i], nv); dec++; }
        }
        printf("modiff done: %d guard(s) decreased\n", dec);
        return 0;
    }
    if (!strcmp(cmd, "snap")) {
        // snap <pid> <outfile> : dump all scannable heap regions to a file
        FILE *out = fopen(argv[3], "wb"); if (!out) { perror("out"); return 1; }
        static Region regs[8192]; int nr = load_regions(pid, regs, 8192);
        char *buf = malloc(64*1024*1024);
        for (int i = 0; i < nr; i++) {
            if (!scannable(&regs[i])) continue;
            size_t sz = regs[i].hi - regs[i].lo; if (sz > 64*1024*1024) sz = 64*1024*1024;
            if (!read_at(regs[i].lo, buf, sz)) continue;
            fwrite(&regs[i].lo, 8, 1, out); fwrite(&sz, 8, 1, out); fwrite(buf, 1, sz, out);
        }
        uint64_t z = 0; fwrite(&z, 8, 1, out); fclose(out);
        printf("snap done\n"); return 0;
    }
    if (!strcmp(cmd, "hunt")) {
        // hunt <pid> <snapfile> <oldval> <newval> : 32-bit words that were oldval and are now newval
        uint32_t oldv = (uint32_t)strtoul(argv[4], 0, 0), newv = (uint32_t)strtoul(argv[5], 0, 0);
        FILE *in = fopen(argv[3], "rb"); if (!in) { perror("in"); return 1; }
        char *buf = malloc(64*1024*1024), *cur = malloc(64*1024*1024);
        uint64_t lo, sz; int hits = 0;
        while (fread(&lo, 8, 1, in) == 1 && lo) {
            if (fread(&sz, 8, 1, in) != 1) break;
            if (fread(buf, 1, sz, in) != sz) break;
            if (!read_at(lo, cur, sz)) continue;
            for (size_t j = 0; j + 4 <= sz; j += 4) {
                if (*(uint32_t*)(buf+j) == oldv && *(uint32_t*)(cur+j) == newv) {
                    printf("HIT %012" PRIx64 "\n", lo + j);
                    if (++hits >= 200000) break;
                }
            }
            if (hits >= 200000) break;
        }
        fclose(in); printf("hunt done: %d\n", hits); return 0;
    }
    if (!strcmp(cmd, "scanm")) {
        // scanm <pid> <val64> <mask64> [max] : 8-byte-aligned qword where (v & mask) == (val & mask); prints addr + full qword
        uint64_t target = strtoull(argv[3], 0, 16), mask = strtoull(argv[4], 0, 16);
        int maxh = argc > 5 ? atoi(argv[5]) : 200; target &= mask;
        static Region regs[8192]; int nr = load_regions(pid, regs, 8192);
        int hits = 0; uint64_t *buf = malloc(64*1024*1024);
        for (int i = 0; i < nr && hits < maxh; i++) {
            if (!scannable(&regs[i])) continue;
            size_t sz = regs[i].hi - regs[i].lo; if (sz > 64*1024*1024) sz = 64*1024*1024;
            if (!read_at(regs[i].lo, buf, sz)) continue;
            size_t cnt = sz/8;
            for (size_t j = 0; j < cnt && hits < maxh; j++)
                if ((buf[j] & mask) == target) { printf("HIT %012" PRIx64 " %016" PRIx64 "\n", regs[i].lo + j*8, buf[j]); hits++; }
        }
        printf("scanm done: %d\n", hits); return 0;
    }
    if (!strcmp(cmd, "sscan")) {
        // sscan <pid> <string> [maxhits] : find an ASCII string in rw heap
        const char *s = argv[3]; size_t slen = strlen(s);
        int maxh = argc > 4 ? atoi(argv[4]) : 60;
        static Region regs[8192]; int nr = load_regions(pid, regs, 8192);
        int hits = 0; char *buf = malloc(64*1024*1024);
        for (int i = 0; i < nr && hits < maxh; i++) {
            if (!scannable(&regs[i])) continue;
            size_t sz = regs[i].hi - regs[i].lo; if (sz > 64*1024*1024) sz = 64*1024*1024;
            if (!read_at(regs[i].lo, buf, sz)) continue;
            for (size_t j = 0; j + slen <= sz && hits < maxh; j++) {
                if (buf[j] == s[0] && memcmp(buf+j, s, slen) == 0) {
                    printf("HIT %012" PRIx64 "\n", regs[i].lo + j); hits++;
                }
            }
        }
        printf("sscan done: %d\n", hits); return 0;
    }
    if (!strcmp(cmd, "scan32")) {
        // scan32 <pid> <hexval32> [maxhits] : 4-byte-aligned 32-bit value in rw heap
        uint32_t target = (uint32_t)strtoull(argv[3], 0, 16);
        int maxh = argc > 4 ? atoi(argv[4]) : 200;
        static Region regs[8192];
        int nr = load_regions(pid, regs, 8192);
        int hits = 0;
        uint32_t *buf = malloc(64*1024*1024);
        for (int i = 0; i < nr && hits < maxh; i++) {
            if (!scannable(&regs[i])) continue;
            size_t sz = regs[i].hi - regs[i].lo; if (sz > 64*1024*1024) sz = 64*1024*1024;
            if (!read_at(regs[i].lo, buf, sz)) continue;
            size_t cnt = sz/4;
            for (size_t j = 0; j < cnt && hits < maxh; j++) {
                if (buf[j] == target) { printf("HIT %012" PRIx64 "\n", regs[i].lo + j*4); hits++; }
            }
        }
        printf("scan32 done: %d hits\n", hits);
        return 0;
    }
    if (!strcmp(cmd, "scan") || !strcmp(cmd, "scanr")) {
        uint64_t target = strtoull(argv[3], 0, 16);
        uint64_t rlo = 0, rhi = ~0ULL; int maxh = 64; int ai = 4;
        if (!strcmp(cmd, "scanr")) { rlo = strtoull(argv[4],0,16); rhi = strtoull(argv[5],0,16); ai = 6; }
        if (argc > ai) maxh = atoi(argv[ai]);
        static Region regs[8192];
        int nr = load_regions(pid, regs, 8192);
        int hits = 0;
        uint64_t *buf = malloc(64*1024*1024);
        for (int i = 0; i < nr && hits < maxh; i++) {
            if (!scannable(&regs[i])) continue;
            if (regs[i].hi <= rlo || regs[i].lo >= rhi) continue;
            uint64_t lo = regs[i].lo < rlo ? rlo : regs[i].lo;
            uint64_t hi = regs[i].hi > rhi ? rhi : regs[i].hi;
            size_t sz = hi - lo; if (sz > 64*1024*1024) sz = 64*1024*1024;
            if (!read_at(lo, buf, sz)) continue;
            size_t cnt = sz/8;
            for (size_t j = 0; j < cnt && hits < maxh; j++) {
                if (buf[j] == target) { printf("HIT %012" PRIx64 "  (region %012" PRIx64 " %s)\n", lo + j*8, regs[i].lo, regs[i].name); hits++; }
            }
        }
        printf("scan done: %d hits\n", hits);
        return 0;
    }
    fprintf(stderr, "unknown cmd %s\n", cmd);
    return 2;
}
