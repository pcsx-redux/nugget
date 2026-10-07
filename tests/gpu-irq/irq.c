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

// GP0(1Fh)/GP1(02h) IRQ flag behavior. See gpu-irq.c for the bit map.

CESTER_BODY(

// Request IRQ1 via GP0(1Fh) and let the GPU consume it before the read.
static void gpuIrqRequest(void) {
    waitGPU();
    GPU_DATA = 0x1f000000u;
    waitGPU();
}

// Acknowledge IRQ1 via GP1(02h). GP1 is a control-port command; give the
// status bit a moment to settle before it is read back.
static void gpuIrqAck(void) {
    sendGPUStatus(0x02000000u);
    for (volatile int i = 0; i < 64; i++) {
        (void)GPU_STATUS;
    }
}

)  // CESTER_BODY

// --------------------------------------------------------------------------
// 1. The IRQ flag is clear after a full reset.
// --------------------------------------------------------------------------
CESTER_TEST(irq_clear_after_reset, gpu_irq,
    rasterReset();
    gpuIrqRequest();  // raise it, so the reset has something to clear
    ASSERT_STAT_EQ(GPUSTAT_IRQ, GPUSTAT_IRQ);
    rasterReset();  // includes GP1(00h) full reset
    ASSERT_STAT_EQ(0u, GPUSTAT_IRQ);
)

// --------------------------------------------------------------------------
// 2. GP0(1Fh) sets GPUSTAT.24.
// --------------------------------------------------------------------------
CESTER_TEST(irq_gp0_1f_sets_flag, gpu_irq,
    rasterReset();
    gpuIrqRequest();
    ASSERT_STAT_EQ(GPUSTAT_IRQ, GPUSTAT_IRQ);
)

// --------------------------------------------------------------------------
// 3. GP1(02h) acknowledges and clears a raised IRQ flag.
// --------------------------------------------------------------------------
CESTER_TEST(irq_gp1_02_acknowledges, gpu_irq,
    rasterReset();
    gpuIrqRequest();          // raise it
    ASSERT_STAT_EQ(GPUSTAT_IRQ, GPUSTAT_IRQ);
    gpuIrqAck();              // GP1(02h)
    ASSERT_STAT_EQ(0u, GPUSTAT_IRQ);
)
