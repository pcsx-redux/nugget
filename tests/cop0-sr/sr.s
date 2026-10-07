/*

MIT License

Copyright (c) 2026 PCSX-Redux authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

    .set noreorder
    .set noat
    .text

/* uint32_t sr_try(uint32_t value, uint32_t restore)
   Writes value to SR, reads it back, writes restore. No memory access
   while value is live, so IsC, SwC and KUc can be written safely. Call
   through the KUSEG alias when value has KUc set. */
    .global sr_try
    .type sr_try, @function
sr_try:
    mtc0  $a0, $12
    nop
    nop
    mfc0  $v0, $12
    nop
    mtc0  $a1, $12
    nop
    nop
    jr    $ra
    nop

/* uint32_t cause_try(uint32_t value)
   Writes value to Cause, reads it back, writes 0. IEc must be clear. */
    .global cause_try
    .type cause_try, @function
cause_try:
    mtc0  $a0, $13
    nop
    nop
    mfc0  $v0, $13
    nop
    mtc0  $0, $13
    nop
    jr    $ra
    nop

/* A cached line whose first word is fetched by calling it. */
    .balign 16
    .global cm_target
    .type cm_target, @function
cm_target:
    jr    $ra
    nop
    nop
    nop

/* void cm_probe(uint32_t hit, uint32_t miss, uint32_t sr_isc, uint32_t out[6])
   Must run uncached (call through KSEG1). Isolated loads: hit, miss, hit.
   SR read after each. Registers only while IsC is set. */
    .global cm_probe
    .type cm_probe, @function
cm_probe:
    lw    $t8, 16($sp)
    lui   $t7, 0xfffe
    lw    $t6, 0x130($t7)
    nop
    sw    $t8, 0x130($t7)
    mfc0  $t9, $12
    nop
    mtc0  $a2, $12
    nop
    nop
    lw    $t0, 0($a0)
    nop
    mfc0  $t1, $12
    nop
    lw    $t2, 0($a1)
    nop
    mfc0  $t3, $12
    nop
    lw    $t4, 0($a0)
    nop
    mfc0  $t5, $12
    nop
    mtc0  $t9, $12
    nop
    nop
    sw    $t6, 0x130($t7)
    sw    $t0, 0($a3)
    sw    $t1, 4($a3)
    sw    $t2, 8($a3)
    sw    $t3, 12($a3)
    sw    $t4, 16($a3)
    jr    $ra
    sw    $t5, 20($a3)

/* void bev_probe(uint32_t sr): syscall with SR = sr, then restore. */
    .global bev_probe
    .type bev_probe, @function
bev_probe:
    mfc0  $t9, $12
    nop
    mtc0  $a0, $12
    nop
    nop
    syscall
    nop
    mtc0  $t9, $12
    nop
    jr    $ra
    nop

/* void pz_probe(uint32_t sr, uint32_t *word, uint32_t out[2])
   Store and reload a word with SR = sr, then read SR. */
    .global pz_probe
    .type pz_probe, @function
pz_probe:
    mfc0  $t9, $12
    nop
    mtc0  $a0, $12
    nop
    nop
    lui   $t0, 0x1234
    ori   $t0, 0x5678
    sw    $t0, 0($a1)
    lw    $t1, 0($a1)
    nop
    mfc0  $t2, $12
    nop
    mtc0  $t9, $12
    nop
    nop
    sw    $t1, 0($a2)
    jr    $ra
    sw    $t2, 4($a2)

/* void user_run(uint32_t data, uint32_t sr, uint32_t code)
   Enters code at the given address with SR = sr (KUp set), rfe pops it
   into the current mode. The user block ends in an exception, and the
   handler resumes at user_resume in kernel mode. */
    .global user_run
    .type user_run, @function
user_run:
    mtc0  $a1, $12
    nop
    nop
    jr    $a2
    rfe
    .global user_resume
user_resume:
    jr    $ra
    nop

/* User blocks. Position independent, t0-t7 only, a0 = data word. */
    .global ue_loads
ue_loads:
    lb    $t0, 0($a0)
    lb    $t1, 1($a0)
    lb    $t2, 3($a0)
    lh    $t3, 0($a0)
    lh    $t4, 2($a0)
    lw    $t5, 0($a0)
    move  $t7, $0
    lwl   $t7, 1($a0)
    nop
    syscall
    nop

    .global ue_sb
ue_sb:
    li    $t6, 0xaa
    sb    $t6, 0($a0)
    lw    $t5, 0($a0)
    nop
    syscall
    nop

    .global ue_sh
ue_sh:
    li    $t6, 0xbbcc
    sh    $t6, 0($a0)
    lw    $t5, 0($a0)
    nop
    syscall
    nop

    .global ue_mfc0
ue_mfc0:
    li    $t0, 0x5ead
    mfc0  $t0, $12
    nop
    syscall
    nop

/* Coprocessor opcode stubs: uint32_t cop_stub_N(uint32_t *scratch)
   t0 preset to a sentinel, the opcode, then t0 returned. a0 = scratch word
   for the load/store forms (encoded with base a0). */
.macro COPSTUB name, op
    .global \name
    .type \name, @function
\name:
    lui   $t0, 0xdead
    ori   $t0, 0xbeef
    .word \op
    nop
    nop
    jr    $ra
    move  $v0, $t0
.endm

    COPSTUB cs_mfc0,  0x40086000  /* mfc0 t0, $12 */
    COPSTUB cs_mfc1,  0x44080000  /* mfc1 t0, $0 */
    COPSTUB cs_cfc1,  0x44480000  /* cfc1 t0, $0 */
    COPSTUB cs_lwc1,  0xc4800000  /* lwc1 $0, 0(a0) */
    COPSTUB cs_swc1,  0xe4800000  /* swc1 $0, 0(a0) */
    COPSTUB cs_cop1,  0x46000001  /* cop1 0x1 */
    COPSTUB cs_mfc2,  0x48080000  /* mfc2 t0, $0 */
    COPSTUB cs_mfc3,  0x4c080000  /* mfc3 t0, $0 */
    COPSTUB cs_cfc3,  0x4c480000  /* cfc3 t0, $0 */
    COPSTUB cs_lwc3,  0xcc800000  /* lwc3 $0, 0(a0) */
    COPSTUB cs_swc3,  0xec800000  /* swc3 $0, 0(a0) */
    COPSTUB cs_cop3,  0x4e000001  /* cop3 0x1 */
