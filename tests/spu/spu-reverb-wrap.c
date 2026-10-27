// ============================================================================
// Reverb work-area extent: where do the writes land?
// ============================================================================
//
// For one psx-spx reverb preset (selected with SPU_REVERB_WRAP_PRESET), runs
// four configurations: mBASE at the size the largest address register needs
// (size = max(m*, d*) * 8 + 8), and mBASE at the psx-spx table size, each with
// reverb master on and off. SPU RAM from 0x1000 to the top is filled with an
// address-dependent marker, voice 1 plays a looping sine into the reverb input
// for ~5 s, then the whole range is read back and compared to the marker.
//
// 0x0000-0x0fff is excluded: the capture buffers are written by the SPU itself.
// 0x1080-0x10ff holds the voice sample. A DMA read returns its last ~60 bytes
// wrong on silicon, so every read over-reads by SPU_RW_GUARD bytes and only the
// head is used. The top SPU_RW_GUARD bytes of SPU RAM have nothing past them to
// over-read into and are reported as untrusted.
//
// Output: OBS lines on stdout, and with SPU_REVERB_WRAP_DUMP a raw image of
// 0x1000-0x7ffff per run over PCdrv (32-byte header, then the bytes).

#ifndef SPU_REVERB_WRAP_HELPERS_DEFINED
#define SPU_REVERB_WRAP_HELPERS_DEFINED

#ifndef SPU_REVERB_WRAP_PRESET
#define SPU_REVERB_WRAP_PRESET 0
#endif

#define SPU_RW_LO 0x1000u
#define SPU_RW_TOP 0x80000u
#define SPU_RW_CHUNK 0x4000u
#define SPU_RW_GUARD 0x100u
#define SPU_RW_SAMPLE_LO SPU_UPLOAD_ADDR
#define SPU_RW_SAMPLE_HI (SPU_UPLOAD_ADDR + 0x80u)
// ~5 s: one bit-11 half is 256 samples at 44.1 kHz.
#define SPU_RW_WINDOWS 861u

struct SPUReverbWrapPreset {
    const char *name;
    uint32_t tableSize;
    uint16_t regs[32];
};

static const struct SPUReverbWrapPreset kWrapPresets[3] = {
    {"half_echo", 0x3c00u,
     {0x0017, 0x0013, 0x70f0, 0x4fa8, 0xbce0, 0x4510, 0xbef0, 0x8500,
      0x5f80, 0x54c0, 0x0371, 0x02af, 0x02e5, 0x01df, 0x02b0, 0x01d7,
      0x0358, 0x026a, 0x01d6, 0x011e, 0x012d, 0x00b1, 0x011f, 0x0059,
      0x01a0, 0x00e3, 0x0058, 0x0040, 0x0028, 0x0014, 0x8000, 0x8000}},
    {"chaos_echo", 0x18040u,
     {0x0001, 0x0001, 0x7fff, 0x7fff, 0x0000, 0x0000, 0x0000, 0x8100,
      0x0000, 0x0000, 0x1fff, 0x0fff, 0x1005, 0x0005, 0x0000, 0x0000,
      0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
      0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000}},
    {"delay", 0x18040u,
     {0x0001, 0x0001, 0x7fff, 0x7fff, 0x0000, 0x0000, 0x0000, 0x0000,
      0x0000, 0x0000, 0x1fff, 0x0fff, 0x1005, 0x0005, 0x0000, 0x0000,
      0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
      0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000}},
};

static uint8_t s_rw_ram[SPU_RW_TOP - SPU_RW_LO] __attribute__((aligned(4)));
static uint8_t s_rw_chunk[SPU_RW_CHUNK + SPU_RW_GUARD] __attribute__((aligned(4)));

static char *spu_rw_cat(char *d, const char *src) {
    while (*src) *d++ = *src++;
    *d = 0;
    return d;
}

static inline uint16_t spu_rw_marker(uint32_t addr) {
    uint32_t x = (addr >> 1) * 0x9e3779b1u;
    return (uint16_t)((x >> 16) ^ 0x5aa5u);
}

// Size the address registers need: every m*/d* is an offset in 8-byte units
// from the buffer position, except dAPF1/dAPF2 (regs 0 and 1), which are
// subtracted from mAPF.
static uint32_t spu_rw_needed_size(const uint16_t *regs) {
    uint32_t maxReg = 0;
    for (unsigned i = 10; i < 30; i++) {
        if (regs[i] > maxReg) maxReg = regs[i];
    }
    return maxReg * 8u + 8u;
}

static void spu_rw_fill(void) {
    for (uint32_t a = SPU_RW_LO; a < SPU_RW_TOP; a += SPU_RW_CHUNK) {
        uint16_t *w = (uint16_t *)s_rw_chunk;
        for (unsigned i = 0; i < SPU_RW_CHUNK / 2; i++) w[i] = spu_rw_marker(a + i * 2u);
        spu_write_sync(a, s_rw_chunk, SPU_RW_CHUNK);
    }
}

static uint8_t s_rw_tail[(SPU_RW_TOP - SPU_RW_LO) / SPU_RW_CHUNK][SPU_RW_GUARD];

// Read 0x1000..0x7ffff into s_rw_ram, each read over-reading by SPU_RW_GUARD.
// The over-read bytes are kept and compared afterwards against the next read's
// head, which covers the same addresses, to measure the bad tail of a read.
static void spu_rw_readback(const char *tag) {
    unsigned chunks = 0;
    for (uint32_t a = SPU_RW_LO; a < SPU_RW_TOP; a += SPU_RW_CHUNK) {
        uint32_t len = SPU_RW_CHUNK + SPU_RW_GUARD;
        if (a + len > SPU_RW_TOP) len = SPU_RW_TOP - a;
        spu_read_sync(a, s_rw_chunk, len);
        uint32_t keep = SPU_RW_CHUNK;
        if (a + keep > SPU_RW_TOP) keep = SPU_RW_TOP - a;
        for (uint32_t i = 0; i < keep; i++) s_rw_ram[a - SPU_RW_LO + i] = s_rw_chunk[i];
        if (len > keep) {
            for (uint32_t i = 0; i < SPU_RW_GUARD; i++) s_rw_tail[chunks][i] = s_rw_chunk[keep + i];
        }
        chunks++;
    }
    // A chunk's guard bytes are the next chunk's first bytes. Report, over every
    // read that had a guard, the earliest bad byte counted from the read's end.
    uint32_t reads = 0, badReads = 0, badBytes = 0, worst = 0, best = 0xffffffffu;
    for (unsigned c = 0; c + 1 < chunks; c++) {
        uint32_t nextLo = SPU_RW_LO + (c + 1) * SPU_RW_CHUNK - SPU_RW_LO;
        if (nextLo + SPU_RW_GUARD > sizeof(s_rw_ram)) break;
        reads++;
        uint32_t firstBad = 0xffffffffu, n = 0;
        for (uint32_t i = 0; i < SPU_RW_GUARD; i++) {
            if (s_rw_tail[c][i] != s_rw_ram[nextLo + i]) {
                n++;
                if (firstBad == 0xffffffffu) firstBad = i;
            }
        }
        if (n) {
            badReads++;
            badBytes += n;
            uint32_t fromEnd = SPU_RW_GUARD - firstBad;
            if (fromEnd > worst) worst = fromEnd;
            if (fromEnd < best) best = fromEnd;
        }
    }
    ramsyscall_printf("OBS %s readtail reads=%d bad_reads=%d bad_bytes=%d first_bad_from_end_min=%d max=%d\n", tag,
                      reads, badReads, badBytes, best == 0xffffffffu ? 0 : best, worst);
}

static int spu_rw_changed(uint32_t addr) {
    const uint16_t *w = (const uint16_t *)&s_rw_ram[addr - SPU_RW_LO];
    return *w != spu_rw_marker(addr);
}

static int spu_rw_excluded(uint32_t addr) {
    return addr >= SPU_RW_SAMPLE_LO && addr < SPU_RW_SAMPLE_HI;
}

// Count changed halfwords in [lo, hi) and print up to 8 changed ranges.
static uint32_t spu_rw_scan(const char *tag, const char *label, uint32_t lo, uint32_t hi, uint32_t *first) {
    uint32_t count = 0, ranges = 0, runStart = 0;
    int inRun = 0;
    *first = 0xffffffffu;
    for (uint32_t a = lo; a < hi; a += 2) {
        int c = !spu_rw_excluded(a) && spu_rw_changed(a);
        if (c) {
            count++;
            if (*first == 0xffffffffu) *first = a;
            if (!inRun) { inRun = 1; runStart = a; }
        } else if (inRun) {
            inRun = 0;
            if (ranges < 8) ramsyscall_printf("OBS %s %s range %05x-%05x\n", tag, label, runStart, a);
            ranges++;
        }
    }
    if (inRun) {
        if (ranges < 8) ramsyscall_printf("OBS %s %s range %05x-%05x\n", tag, label, runStart, hi);
        ranges++;
    }
    ramsyscall_printf("OBS %s %s lo=%05x hi=%05x changed_halfwords=%d ranges=%d first=%05x\n", tag, label, lo, hi,
                      count, ranges, *first == 0xffffffffu ? 0xfffff : *first);
    return count;
}

#ifdef SPU_REVERB_WRAP_DUMP
static uint32_t s_rw_hdr[8];
static int s_rw_pcdrv_init = 0;
static void spu_rw_dump(const char *name, uint32_t preset, uint32_t arm, uint32_t revOn, uint32_t base) {
    s_rw_hdr[0] = 0x33353644u;  // 'D653'
    s_rw_hdr[1] = sizeof(s_rw_hdr);
    s_rw_hdr[2] = preset;
    s_rw_hdr[3] = arm;
    s_rw_hdr[4] = revOn;
    s_rw_hdr[5] = base;
    s_rw_hdr[6] = SPU_RW_LO;
    s_rw_hdr[7] = sizeof(s_rw_ram);
    if (!s_rw_pcdrv_init) {
        PCinit();
        s_rw_pcdrv_init = 1;
    }
    int fd = PCcreat(name, 0);
    if (fd < 0) {
        ramsyscall_printf("OBS dump %s PCcreat failed %d\n", name, fd);
        return;
    }
    PCwrite(fd, s_rw_hdr, sizeof(s_rw_hdr));
    PCwrite(fd, s_rw_ram, sizeof(s_rw_ram));
    PCclose(fd);
    ramsyscall_printf("OBS dump %s written\n", name);
}
#endif

static void spu_rw_run(unsigned presetIndex, unsigned arm, int revOn) {
    const struct SPUReverbWrapPreset *p = &kWrapPresets[presetIndex];
    uint32_t needed = spu_rw_needed_size(p->regs);
    uint32_t size = arm == 1 ? needed : p->tableSize;
    uint32_t base = SPU_RW_TOP - size;
    uint32_t needBase = SPU_RW_TOP - needed;
    uint32_t tableBase = SPU_RW_TOP - p->tableSize;
    static char tag[48];
    char *t = spu_rw_cat(tag, "duel1653 ");
    t = spu_rw_cat(t, p->name);
    t = spu_rw_cat(t, arm == 1 ? " arm1 rev" : " arm2 rev");
    spu_rw_cat(t, revOn ? "on" : "off");

    spu_reset_quiet();
    SPU_CTRL = 0x8000 | 0x4000;
    SPU_REVERB_ADDR = base >> 3;
    volatile uint16_t *regs = (volatile uint16_t *)SPU_REVERB;
    for (unsigned i = 0; i < 32; i++) regs[i] = p->regs[i];

    spu_rw_fill();
    for (int i = 0; i < 64; i++) s_upload[i] = kAdpcmSine[i];
    for (int i = 64; i < 128; i++) s_upload[i] = 0xaa;
    spu_write_sync(SPU_UPLOAD_ADDR, s_upload, 128);

    SPU_VOICES[1].volumeLeft = 0x3fff;
    SPU_VOICES[1].volumeRight = 0x3fff;
    SPU_VOL_MAIN_LEFT = 0;
    SPU_VOL_MAIN_RIGHT = 0;
    SPU_REVERB_LEFT = 0;
    SPU_REVERB_RIGHT = 0;
    SPU_REVERB_EN_LOW = 1u << 1;
    SPU_REVERB_EN_HIGH = 0;
    SPU_CTRL = 0x8000 | 0x4000 | (revOn ? 0x0080 : 0);

    spu_wait_status_bit11_flip();
    spu_voice1_keyon(SPU_UPLOAD_ADDR, 0x1000);
    for (unsigned i = 0; i < SPU_RW_WINDOWS; i++) spu_wait_status_bit11_flip();

    // Stop reverb writes before reading back.
    SPU_CTRL = 0x8000 | 0x4000;
    SPU_KEY_OFF_LOW = 0xffff;
    SPU_KEY_OFF_HIGH = 0xffff;
    for (unsigned i = 0; i < 4; i++) spu_wait_status_bit11_flip();
    spu_rw_readback(tag);
    SPU_REVERB_EN_LOW = 0;
    SPU_REVERB_EN_HIGH = 0;

    ramsyscall_printf("OBS %s base=%05x mbase_reg=%04x needed=%05x need_base=%05x table=%05x table_base=%05x "
                      "untrusted=%05x-%05x\n",
                      tag, base, base >> 3, needed, needBase, p->tableSize, tableBase, SPU_RW_TOP - SPU_RW_GUARD,
                      SPU_RW_TOP);
    uint32_t first;
    uint32_t trustedTop = SPU_RW_TOP - SPU_RW_GUARD;
    // Whole trusted range: the lowest changed address anywhere.
    spu_rw_scan(tag, "all", SPU_RW_LO, trustedTop, &first);
    spu_rw_scan(tag, "below_base", SPU_RW_LO, base, &first);
    if (arm == 2) spu_rw_scan(tag, "padding", tableBase, needBase, &first);
    spu_rw_scan(tag, "need_area", needBase, trustedTop, &first);

#ifdef SPU_REVERB_WRAP_DUMP
    static char name[48];
    char *n = spu_rw_cat(name, "d1653-");
    n = spu_rw_cat(n, p->name);
    n = spu_rw_cat(n, arm == 1 ? "-arm1-" : "-arm2-");
    spu_rw_cat(n, revOn ? "on.bin" : "off.bin");
    spu_rw_dump(name, presetIndex, arm, revOn, base);
#endif
    muteSpu();
}

#endif

CESTER_TEST(reverb_wrap_extent, spu_tests,
    // Control first, so a readback fault shows before any reverb run.
    spu_rw_run(SPU_REVERB_WRAP_PRESET, 1, 0);
    spu_rw_run(SPU_REVERB_WRAP_PRESET, 1, 1);
    spu_rw_run(SPU_REVERB_WRAP_PRESET, 2, 0);
    spu_rw_run(SPU_REVERB_WRAP_PRESET, 2, 1);
    ramsyscall_printf("OBS duel1653 done preset=%d\n", SPU_REVERB_WRAP_PRESET);
)
