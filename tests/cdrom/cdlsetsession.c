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

// clang-format off

CESTER_BODY(
    // Waits for one IRQ, up to timeoutUs. Returns its cause, or 0 if none came.
    static uint8_t sqWait(uint32_t timeoutUs, uint8_t response[16], uint8_t* size) {
        uint32_t timeout = timeoutUs;
        if (!waitCDRomIRQWithTimeout(&timeout)) {
            *size = 0;
            return 0;
        }
        uint8_t cause = ackCDRomCause();
        *size = readResponse(response);
        return cause;
    }

    static void sqSend(uint8_t cmd, const uint8_t* params, int n) {
        CDROM_REG0 = 0;
        for (int i = 0; i < n; i++) CDROM_REG2 = params[i];
        CDROM_REG1 = cmd;
    }
)

CESTER_TEST(cdlSetSession1, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }
    initializeTime();

    uint8_t session = 1;
    uint8_t response[16] = {0};
    uint8_t size;
    sqSend(CDL_READT, &session, 1);
    uint8_t cause1 = sqWait(1000000, response, &size);
    uint8_t stat1 = response[0];
    uint8_t size1 = size;
    uint8_t cause2 = sqWait(5000000, response, &size);
    uint32_t completeTime = updateTime();

    ramsyscall_printf("Setsession 1: ack cause %i, complete cause %i in %ius\n", cause1, cause2, completeTime);
    cester_assert_uint_eq(3, cause1);
    cester_assert_uint_eq(1, size1);
    cester_assert_uint_eq(2, stat1);
    cester_assert_uint_eq(2, cause2);
    cester_assert_uint_eq(1, size);
    cester_assert_uint_eq(2, response[0]);
    // 2.64 s on a SCPH-9002.
    cester_assert_uint_ge(completeTime, 2000000);
    cester_assert_uint_lt(completeTime, 3500000);
)

CESTER_TEST(cdlSetSession0, test_instance,
    int resetDone = resetCDRom();
    if (!resetDone) {
        cester_assert_true(resetDone);
        return;
    }
    initializeTime();

    uint8_t session = 0;
    uint8_t response[16] = {0};
    uint8_t size;
    sqSend(CDL_READT, &session, 1);
    uint8_t cause = sqWait(1000000, response, &size);

    ramsyscall_printf("Setsession 0: cause %i, %i bytes %02x %02x\n", cause, size, response[0], response[1]);
    cester_assert_uint_eq(5, cause);
    cester_assert_uint_eq(2, size);
    cester_assert_uint_eq(3, response[0]);
    cester_assert_uint_eq(0x10, response[1]);
)
