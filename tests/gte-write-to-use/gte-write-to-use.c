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

#include "gwu.h"

#define FAMILIES(X) X(lighting) X(singles) X(perspective) X(math) X(dpc) X(color) X(mvmva) X(misc)

#define DECL(f) extern const GwuCell gwu_cells_##f[]; extern const int gwu_count_##f;
FAMILIES(DECL)
#define FAM(f) {gwu_cells_##f, &gwu_count_##f},
static const struct {
    const GwuCell* cells;
    const int* count;
} s_families[] = {FAMILIES(FAM)};

static void mask(probe_result_t* r, const GwuCell* c) {
    if (c->ctrl) return;
    switch (c->reg) {
        case 8: r->ir0 = 0; break;
        case 9: r->ir1 = 0; break;
        case 10: r->ir2 = 0; break;
        case 11: r->ir3 = 0; break;
        case 12: r->sxy0 = 0; break;
        case 13: r->sxy1 = 0; break;
        case 14: r->sxy2 = 0; break;
        case 16: r->sz0 = 0; break;
        case 17: r->sz1 = 0; break;
        case 18: r->sz2 = 0; break;
        case 19: r->sz3 = 0; break;
        case 20: r->rgb0 = 0; break;
        case 21: r->rgb1 = 0; break;
        case 22: r->rgb2 = 0; break;
        case 24: r->mac0 = 0; break;
        case 25: r->mac1 = 0; break;
        case 26: r->mac2 = 0; break;
        case 27: r->mac3 = 0; break;
    }
}

// which: 0 = old reference, 1 = new reference, 2 = old then new.
// Returns the written register's value after the command.
static uint32_t run(const GwuCell* c, int n, int uncached, int which, probe_result_t* r, uint32_t* old) {
    GwuFn f = c->fn[n];
    if (uncached) f = (GwuFn)((uintptr_t)f | 0xa0000000u);
    for (int pass = 0; pass < 2; pass++) {
        c->scene();
        *old = c->read();
        f(which == 1 ? c->canary : *old, which == 0 ? *old : c->canary);
    }
    read_full_state(r);
    return c->read();
}

// O / N: the command's outputs match the old / new reference.
// o / n: same, once the written register is left out, for a register the
// command also writes (the write landed after the command's own result).
// B: the two references agree, so the cell cannot tell. X: neither.
// For a mixed result, which outputs saw what: one letter per probe_result_t
// field in declaration order (RGB0-2, MAC0-3, IR0-3, SXY0-2, SZ0-3, FLAG),
// '.' where the two references agree.
_Static_assert(sizeof(probe_result_t) == 19 * sizeof(uint32_t), "probe_result_t layout");
static void printFields(const GwuCell* c, int n, const probe_result_t* o, const probe_result_t* nw,
                        const probe_result_t* a) {
    const uint32_t* po = (const uint32_t*)o;
    const uint32_t* pn = (const uint32_t*)nw;
    const uint32_t* pa = (const uint32_t*)a;
    char f[20];
    for (int i = 0; i < 19; i++) f[i] = po[i] == pn[i] ? '.' : pa[i] == po[i] ? 'O' : pa[i] == pn[i] ? 'N' : 'X';
    f[19] = 0;
    ramsyscall_printf("GWU-FIELDS %s N=%d %s\n", c->label, n, f);
}

static char classify(const GwuCell* c, int n, int uncached) {
    probe_result_t o, nw, a;
    uint32_t oldv, ignored;
    uint32_t opost = run(c, n, uncached, 0, &o, &oldv);
    uint32_t npost = run(c, n, uncached, 1, &nw, &ignored);
    run(c, n, uncached, 2, &a, &ignored);
    int kept = !c->ctrl && !(opost == oldv && npost == c->canary);
    if (!kept) {
        mask(&o, c);
        mask(&nw, c);
        mask(&a, c);
    }
    if (results_equal(&o, &nw)) return 'B';
    if (results_equal(&a, &o)) return 'O';
    if (results_equal(&a, &nw)) return 'N';
    if (!kept) {
        if (!uncached) printFields(c, n, &o, &nw, &a);
        return 'X';
    }
    mask(&o, c);
    mask(&nw, c);
    mask(&a, c);
    if (results_equal(&o, &nw)) return 'X';
    if (results_equal(&a, &o)) return 'o';
    if (results_equal(&a, &nw)) return 'n';
    if (!uncached) printFields(c, n, &o, &nw, &a);
    return 'X';
}

int main(void) {
    gte_enable();
    uint32_t sr = irq_disable();
    ramsyscall_printf("GWU-START\n");
    int cells = 0, blind = 0, late = 0, lateTotal = 0, mismatches = 0;
    static const char* const modes[3] = {"C0", "C1", "U0"};
    for (int m = 0; m < 3; m++) {
        int uncached = m == 2;
        for (unsigned fam = 0; fam < sizeof(s_families) / sizeof(s_families[0]); fam++) {
            for (int i = 0; i < *s_families[fam].count; i++) {
                const GwuCell* c = &s_families[fam].cells[i];
                char res[GWU_NMAX + 1];
                for (int n = 0; n < GWU_NMAX; n++) res[n] = classify(c, n, uncached);
                res[GWU_NMAX] = 0;
                if (!uncached) {
                    for (int n = 0; n < GWU_NMAX; n++) {
                        if (res[n] == c->expect[n] || c->expect[n] == '?') continue;
                        mismatches++;
                        ramsyscall_printf("GWU-MISMATCH %s %s N=%d expected %c got %c\n", modes[m], c->label, n,
                                          c->expect[n], res[n]);
                    }
                }
                if (m == 0) {
                    cells++;
                    if (res[GWU_NMAX - 1] == 'B') blind++;
                }
                if (res[GWU_NMAX - 1] != 'B') {
                    lateTotal++;
                    if (res[GWU_NMAX - 1] == 'N' || res[GWU_NMAX - 1] == 'n') late++;
                }
                ramsyscall_printf("GWU %s %s = %s\n", modes[m], c->label, res);
            }
        }
    }
    ramsyscall_printf("GWU-DONE cells=%d blind=%d late_new=%d/%d\n", cells, blind, late, lateTotal);
    irq_restore(sr);
    ramsyscall_printf("GWU-%s mismatches=%d\n", mismatches ? "FAIL" : "PASS", mismatches);
    return mismatches ? 1 : 0;
}
