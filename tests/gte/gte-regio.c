// GTE register I/O tests: data/control register read/write, sign extension,
// SXY FIFO, IRGB/ORGB, LZCS/LZCR, FLAG register, CTC2 sign extension.

// ==========================================================================
// Data register roundtrip and sign/zero extension
// ==========================================================================

CESTER_TEST(regio_mac0_roundtrip, gte_tests,
    cop2_put(24, 0x12345678);
    uint32_t out;
    cop2_get(24, out);
    cester_assert_uint_eq(0x12345678, out);
)

CESTER_TEST(regio_mac1_roundtrip, gte_tests,
    cop2_put(25, 0xdeadbeef);
    uint32_t out;
    cop2_get(25, out);
    cester_assert_uint_eq(0xdeadbeef, out);
)

CESTER_TEST(regio_ir0_sign_extend, gte_tests,
    cop2_put(8, 0x0000ffff);
    uint32_t out;
    cop2_get(8, out);
    cester_assert_uint_eq(0xffffffff, out);
)

CESTER_TEST(regio_ir1_sign_extend, gte_tests,
    cop2_put(9, 0x00008000);
    uint32_t out;
    cop2_get(9, out);
    cester_assert_uint_eq(0xffff8000, out);
)

CESTER_TEST(regio_ir2_positive, gte_tests,
    cop2_put(10, 0x00001234);
    uint32_t out;
    cop2_get(10, out);
    cester_assert_uint_eq(0x00001234, out);
)

CESTER_TEST(regio_ir3_positive, gte_tests,
    cop2_put(11, 0x00007fff);
    uint32_t out;
    cop2_get(11, out);
    cester_assert_uint_eq(0x00007fff, out);
)

CESTER_TEST(regio_vz0_sign_extend, gte_tests,
    cop2_put(1, 0x0000ff00);
    uint32_t out;
    cop2_get(1, out);
    cester_assert_uint_eq(0xffffff00, out);
)

CESTER_TEST(regio_vxy0_packed, gte_tests,
    cop2_put(0, 0x00640032);
    uint32_t out;
    cop2_get(0, out);
    cester_assert_uint_eq(0x00640032, out);
)

CESTER_TEST(regio_otz_zero_extend, gte_tests,
    cop2_put(7, 0xffffffff);
    uint32_t out;
    cop2_get(7, out);
    cester_assert_uint_eq(0x0000ffff, out);
)

CESTER_TEST(regio_sz_zero_extend, gte_tests,
    cop2_put(16, 0xdeadbeef);
    uint32_t out;
    cop2_get(16, out);
    cester_assert_uint_eq(0x0000beef, out);
)

CESTER_TEST(regio_rgbc_roundtrip, gte_tests,
    cop2_put(6, 0xaa554080);
    uint32_t out;
    cop2_get(6, out);
    cester_assert_uint_eq(0xaa554080, out);
)

CESTER_TEST(regio_res1_readwrite, gte_tests,
    cop2_put(23, 0xdeadbeef);
    uint32_t out;
    cop2_get(23, out);
    cester_assert_uint_eq(0xdeadbeef, out);
)

// ==========================================================================
// SXY FIFO
// ==========================================================================

CESTER_TEST(regio_sxy_fifo_push, gte_tests,
    cop2_put(12, 0x00010002);
    cop2_put(13, 0x00030004);
    cop2_put(14, 0x00050006);
    cop2_put(15, 0x00070008);
    uint32_t sxy0, sxy1, sxy2;
    cop2_get(12, sxy0);
    cop2_get(13, sxy1);
    cop2_get(14, sxy2);
    cester_assert_uint_eq(0x00030004, sxy0);
    cester_assert_uint_eq(0x00050006, sxy1);
    cester_assert_uint_eq(0x00070008, sxy2);
)

CESTER_TEST(regio_sxyp_read_returns_sxy2, gte_tests,
    cop2_put(14, 0xaabbccdd);
    uint32_t sxyp;
    cop2_get(15, sxyp);
    cester_assert_uint_eq(0xaabbccdd, sxyp);
)

CESTER_TEST(regio_sxy_fifo_triple_push, gte_tests,
    cop2_put(15, 0x11111111);
    cop2_put(15, 0x22222222);
    cop2_put(15, 0x33333333);
    uint32_t sxy0, sxy1, sxy2;
    cop2_get(12, sxy0);
    cop2_get(13, sxy1);
    cop2_get(14, sxy2);
    cester_assert_uint_eq(0x11111111, sxy0);
    cester_assert_uint_eq(0x22222222, sxy1);
    cester_assert_uint_eq(0x33333333, sxy2);
)

// ==========================================================================
// IRGB / ORGB
// ==========================================================================

CESTER_TEST(regio_irgb_expand, gte_tests,
    cop2_put(28, 0x7fff);
    __asm__ volatile("nop; nop; nop; nop");
    uint32_t ir1, ir2, ir3;
    cop2_get(9, ir1);
    cop2_get(10, ir2);
    cop2_get(11, ir3);
    cester_assert_uint_eq(0x00000f80, ir1);
    cester_assert_uint_eq(0x00000f80, ir2);
    cester_assert_uint_eq(0x00000f80, ir3);
)

CESTER_TEST(regio_irgb_individual, gte_tests,
    cop2_put(28, 0x000a);  // R=10, G=0, B=0
    __asm__ volatile("nop; nop; nop; nop");
    uint32_t ir1, ir2, ir3;
    cop2_get(9, ir1);
    cop2_get(10, ir2);
    cop2_get(11, ir3);
    cester_assert_uint_eq(0x00000500, ir1);  // 10 << 7
    cester_assert_uint_eq(0x00000000, ir2);
    cester_assert_uint_eq(0x00000000, ir3);
)

CESTER_TEST(regio_orgb_pack, gte_tests,
    cop2_put(9, 0x0f80);
    cop2_put(10, 0x0f80);
    cop2_put(11, 0x0f80);
    uint32_t orgb;
    cop2_get(29, orgb);
    cester_assert_uint_eq(0x7fff, orgb);
)

// ORGB saturates, not truncates (psx-spx correct, Sony SDK wrong)
CESTER_TEST(regio_orgb_saturate_negative, gte_tests,
    cop2_put(9, 0xffff8000);  // IR1 = -32768 (negative)
    cop2_put(10, 0x00002000); // IR2 = 8192 (large positive)
    cop2_put(11, 0x00000380); // IR3 = 896 (normal)
    uint32_t orgb;
    cop2_get(29, orgb);
    uint32_t r = orgb & 0x1f;
    uint32_t g = (orgb >> 5) & 0x1f;
    uint32_t b = (orgb >> 10) & 0x1f;
    cester_assert_uint_eq(0, r);    // negative saturated to 0
    cester_assert_uint_eq(31, g);   // large saturated to 0x1f
    cester_assert_uint_eq(7, b);    // 896 >> 7 = 7
)

CESTER_TEST(regio_orgb_saturate_large, gte_tests,
    cop2_put(9, 0x1000);
    cop2_put(10, 0x1000);
    cop2_put(11, 0x1000);
    uint32_t orgb;
    cop2_get(29, orgb);
    // 0x1000>>7 = 0x20 = 32, saturated to 31
    cester_assert_uint_eq(0x7fff, orgb);
)

// ==========================================================================
// LZCS / LZCR
// ==========================================================================

CESTER_TEST(regio_lzcr_zero, gte_tests,
    cop2_put(30, 0x00000000);
    uint32_t lzcr;
    cop2_get(31, lzcr);
    cester_assert_uint_eq(32, lzcr);
)

CESTER_TEST(regio_lzcr_all_ones, gte_tests,
    cop2_put(30, 0xffffffff);
    uint32_t lzcr;
    cop2_get(31, lzcr);
    cester_assert_uint_eq(32, lzcr);
)

CESTER_TEST(regio_lzcr_one, gte_tests,
    cop2_put(30, 0x00000001);
    uint32_t lzcr;
    cop2_get(31, lzcr);
    cester_assert_uint_eq(31, lzcr);
)

CESTER_TEST(regio_lzcr_msb_set, gte_tests,
    cop2_put(30, 0x80000000);
    uint32_t lzcr;
    cop2_get(31, lzcr);
    cester_assert_uint_eq(1, lzcr);
)

CESTER_TEST(regio_lzcr_positive_mid, gte_tests,
    cop2_put(30, 0x00010000);
    uint32_t lzcr;
    cop2_get(31, lzcr);
    cester_assert_uint_eq(15, lzcr);
)

CESTER_TEST(regio_lzcr_negative_mid, gte_tests,
    cop2_put(30, 0xfffe0000);
    uint32_t lzcr;
    cop2_get(31, lzcr);
    cester_assert_uint_eq(15, lzcr);
)

// ==========================================================================
// FLAG register
// ==========================================================================

CESTER_TEST(regio_flag_write_mask, gte_tests,
    cop2_putc(31, 0xffffffff);
    uint32_t flag = gte_read_flag();
    cester_assert_uint_eq(0xfffff000, flag);
)

// Writing all-ones cannot tell 7FFFF000h from FFFFF000h: under the first, bit31 is
// dropped on write and recomputed to 1 from the bits 13-18/23-30 that all-ones also
// sets, so both masks yield 0xfffff000. Bit31 on its own is the discriminator.
CESTER_TEST(regio_flag_bit31_not_writable, gte_tests,
    cop2_putc(31, 1u << 31);
    uint32_t flag = gte_read_flag();
    ramsyscall_printf("FLAG bit31 alone reads back 0x%08x\n", flag);
    cester_assert_uint_eq(0, flag);
)

CESTER_TEST(regio_flag_low_bits_masked, gte_tests,
    cop2_putc(31, 0x00000fff);
    uint32_t flag = gte_read_flag();
    cester_assert_uint_eq(0, flag);
)

CESTER_TEST(regio_flag_bit12_no_summary, gte_tests,
    cop2_putc(31, (1 << 12));
    uint32_t flag = gte_read_flag();
    cester_assert_uint_eq((1 << 12), flag);
)

CESTER_TEST(regio_flag_bits19_22_no_summary, gte_tests,
    uint32_t flag;
    int ok = 1;
    int i;
    for (i = 19; i <= 22; i++) {
        cop2_putc(31, (1u << i));
        flag = gte_read_flag();
        if (flag != (1u << i)) ok = 0;
    }
    cester_assert_int_eq(1, ok);
)

CESTER_TEST(regio_flag_bits13_18_set_summary, gte_tests,
    uint32_t flag;
    int ok = 1;
    int i;
    for (i = 13; i <= 18; i++) {
        cop2_putc(31, (1u << i));
        flag = gte_read_flag();
        if (flag != ((1u << i) | (1u << 31))) ok = 0;
    }
    cester_assert_int_eq(1, ok);
)

CESTER_TEST(regio_flag_bits23_30_set_summary, gte_tests,
    uint32_t flag;
    int ok = 1;
    int i;
    for (i = 23; i <= 30; i++) {
        cop2_putc(31, (1u << i));
        flag = gte_read_flag();
        if (flag != ((1u << i) | (1u << 31))) ok = 0;
    }
    cester_assert_int_eq(1, ok);
)

// ==========================================================================
// Control register sign extension
// ==========================================================================

CESTER_TEST(regio_ctrl_r33_sign_extend, gte_tests,
    cop2_putc(4, 0x00008000);
    uint32_t out;
    cop2_getc(4, out);
    cester_assert_uint_eq(0xffff8000, out);
)

CESTER_TEST(regio_ctrl_zsf3_sign_extend, gte_tests,
    cop2_putc(29, 0x0000ffff);
    uint32_t out;
    cop2_getc(29, out);
    cester_assert_uint_eq(0xffffffff, out);
)

// H register sign-extension bug (psx-spx documented, Sony omitted)
CESTER_TEST(regio_h_sign_extension_bug, gte_tests,
    cop2_putc(26, 0x8000);
    uint32_t h;
    cop2_getc(26, h);
    cester_assert_uint_eq(0xffff8000, h);
)

CESTER_TEST(regio_h_positive, gte_tests,
    cop2_putc(26, 0x7fff);
    uint32_t h;
    cop2_getc(26, h);
    cester_assert_uint_eq(0x00007fff, h);
)

// All single-16bit control regs sign-extend
CESTER_TEST(regio_ctc2_sign_extend_all, gte_tests,
    uint32_t out;
    int ok = 1;
    // R33(4), L33(12), LB3(20), H(26), DQA(27), ZSF3(29), ZSF4(30)
    cop2_putc(4, 0x8000);  cop2_getc(4, out);  if (out != 0xffff8000) ok = 0;
    cop2_putc(12, 0x8000); cop2_getc(12, out); if (out != 0xffff8000) ok = 0;
    cop2_putc(20, 0x8000); cop2_getc(20, out); if (out != 0xffff8000) ok = 0;
    cop2_putc(26, 0x8000); cop2_getc(26, out); if (out != 0xffff8000) ok = 0;
    cop2_putc(27, 0x8000); cop2_getc(27, out); if (out != 0xffff8000) ok = 0;
    cop2_putc(29, 0x8000); cop2_getc(29, out); if (out != 0xffff8000) ok = 0;
    cop2_putc(30, 0x8000); cop2_getc(30, out); if (out != 0xffff8000) ok = 0;
    cester_assert_int_eq(1, ok);
)

// lm flag clamp behavior
CESTER_TEST(regio_lm_clamp, gte_tests,
    // GPF sf=1 lm=0: IR clamp -0x8000..0x7fff
    cop2_put(8, 0x1000);
    cop2_put(9, 0xffff8000);
    cop2_put(10, 0x100);
    cop2_put(11, 0x7fff);
    cop2_put(6, 0x00808080);
    gte_clear_flag();
    cop2_cmd(COP2_GPF(1, 0));
    int32_t mac1_lm0;
    uint32_t ir1_lm0;
    cop2_get(25, mac1_lm0);
    cop2_get(9, ir1_lm0);
    // GPF sf=1 lm=1
    cop2_put(8, 0x1000);
    cop2_put(9, 0xffff8000);
    cop2_put(10, 0x100);
    cop2_put(11, 0x7fff);
    cop2_put(6, 0x00808080);
    gte_clear_flag();
    cop2_cmd(COP2_GPF(1, 1));
    int32_t mac1_lm1;
    uint32_t ir1_lm1;
    cop2_get(25, mac1_lm1);
    cop2_get(9, ir1_lm1);
    cester_assert_int_eq(-32768, mac1_lm0);
    cester_assert_int_eq(-32768, mac1_lm1);
    cester_assert_uint_eq(0xffff8000, ir1_lm0);  // lm=0: stays -32768
    cester_assert_uint_eq(0x00000000, ir1_lm1);  // lm=1: clamped to 0
)

// ==========================================================================
// 32bit control registers: read-back fidelity, and whether commands write them
//
// psx-spx leaves an unanswered "(Input?, R/W?)" on TR (cnt5-7), BK
// (cnt13-15), FC (cnt21-23), screen offset (cnt24-25) and DQB (cnt28). The
// sign-extension tests above cover the 16bit control registers only.
//
// The assertion is that all-ones does not read back as zero. A "two reads
// agree" check does not work here: it passes on a register stuck at zero.
// ==========================================================================

#define GTE_CTRL32_EACH(M) \
    M(5) M(6) M(7) M(13) M(14) M(15) M(21) M(22) M(23) M(24) M(25) M(28)

#define GTE_CTRL32_PROBE(r)                                     \
    {                                                           \
        uint32_t _a;                                            \
        cop2_putc(r, _pat);                                     \
        cop2_getc(r, _a);                                       \
        if (_pat == 0xffffffffu && _a == 0u) _responds = 0;     \
        ramsyscall_printf(" c%d=0x%08x", (r), _a);              \
    }

#define GTE_CTRL32_SET(r) cop2_putc(r, 0x0bad0000u | (r));

#define GTE_CTRL32_CHECK(r)                                                   \
    {                                                                         \
        uint32_t _o;                                                          \
        cop2_getc(r, _o);                                                     \
        if (_o != (0x0bad0000u | (r))) {                                      \
            _changed++;                                                       \
            ramsyscall_printf("CTRL32 %s wrote c%d: 0x%08x\n", _nm, (r), _o); \
        }                                                                     \
    }

// FLAG is the one control register that is an output. Seed it with a pattern no
// command can reproduce (all-ones reads back as 0xfffff000) and check the seed is
// gone afterwards: that is what "the command rewrites FLAG" means, and a command
// whose own result is zero would be invisible to an "it changed" test.
#define GTE_CTRL32_CMDPROBE(NAME, OP)                                       \
    do {                                                                    \
        const char *_nm = (NAME);                                           \
        uint32_t _flag;                                                     \
        GTE_CTRL32_EACH(GTE_CTRL32_SET)                                     \
        cop2_putc(31, 0xffffffffu);                                         \
        cop2_cmd(OP);                                                       \
        cop2_getc(31, _flag);                                               \
        if (_flag == 0xfffff000u) {                                         \
            _flagkept++;                                                    \
            ramsyscall_printf("CTRL32 %s left FLAG seeded\n", _nm);         \
        }                                                                   \
        ramsyscall_printf("CTRL32 %s FLAG=0x%08x\n", _nm, _flag);           \
        GTE_CTRL32_EACH(GTE_CTRL32_CHECK)                                   \
    } while (0)

CESTER_TEST(regio_ctrl32_readback_table, gte_tests,
    static const uint32_t pats[] = {
        0x00000000u, 0xffffffffu, 0x12345678u, 0xa5a5a5a5u,
        0x80000000u, 0x7fffffffu, 0x0000ffffu, 0xffff0000u,
    };
    int _responds = 1;
    for (unsigned p = 0; p < sizeof(pats) / sizeof(pats[0]); p++) {
        uint32_t _pat = pats[p];
        ramsyscall_printf("CTRL32 pat=0x%08x", _pat);
        GTE_CTRL32_EACH(GTE_CTRL32_PROBE)
        ramsyscall_printf("\n");
    }
    cester_assert_int_eq(1, _responds);
)

CESTER_TEST(regio_ctrl32_command_writes, gte_tests,
    int _changed = 0;
    int _flagkept = 0;
    cop2_put(0, 0x00200010); cop2_put(1, 0x00000040);
    cop2_put(2, 0x0018000c); cop2_put(3, 0x00000050);
    cop2_put(4, 0x00140008); cop2_put(5, 0x00000060);
    cop2_put(6, 0x00808080); cop2_put(8, 0x1000);
    GTE_CTRL32_CMDPROBE("RTPS",  COP2_RTPS(0, 0));
    GTE_CTRL32_CMDPROBE("RTPT",  COP2_RTPT(0, 0));
    GTE_CTRL32_CMDPROBE("NCLIP", COP2_NCLIP);
    GTE_CTRL32_CMDPROBE("AVSZ3", COP2_AVSZ3);
    GTE_CTRL32_CMDPROBE("AVSZ4", COP2_AVSZ4);
    GTE_CTRL32_CMDPROBE("SQR",   COP2_SQR(0, 0));
    GTE_CTRL32_CMDPROBE("DPCS",  COP2_DPCS(0, 0));
    GTE_CTRL32_CMDPROBE("DPCT",  COP2_DPCT(0, 0));
    GTE_CTRL32_CMDPROBE("DCPL",  COP2_DCPL(0, 0));
    GTE_CTRL32_CMDPROBE("INTPL", COP2_INTPL(0, 0));
    GTE_CTRL32_CMDPROBE("NCS",   COP2_NCS(0, 0));
    GTE_CTRL32_CMDPROBE("NCT",   COP2_NCT(0, 0));
    GTE_CTRL32_CMDPROBE("NCCS",  COP2_NCCS(0, 0));
    GTE_CTRL32_CMDPROBE("NCCT",  COP2_NCCT(0, 0));
    GTE_CTRL32_CMDPROBE("NCDS",  COP2_NCDS(0, 0));
    GTE_CTRL32_CMDPROBE("NCDT",  COP2_NCDT(0, 0));
    GTE_CTRL32_CMDPROBE("CC",    COP2_CC(0, 0));
    GTE_CTRL32_CMDPROBE("CDP",   COP2_CDP(0, 0));
    GTE_CTRL32_CMDPROBE("GPF",   COP2_GPF(0, 0));
    GTE_CTRL32_CMDPROBE("GPL",   COP2_GPL(0, 0));
    GTE_CTRL32_CMDPROBE("MVMVA", COP2_MVMVA(0, 0, 0, 0, 0));
    ramsyscall_printf("CTRL32 command-write count: %d, FLAG kept: %d\n", _changed, _flagkept);
    cester_assert_int_eq(0, _changed);
    cester_assert_int_eq(0, _flagkept);
)
