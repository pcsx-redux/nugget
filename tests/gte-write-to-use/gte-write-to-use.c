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
static uint32_t run(const GwuCell* c, int n, int a, int uncached, int which, probe_result_t* r, uint32_t* old) {
    GwuFn f = c->fn[n][a];
    if (uncached) f = (GwuFn)((uintptr_t)f | 0xa0000000u);
    for (int pass = 0; pass < 2; pass++) {
        c->scene();
        *old = c->read();
        f(which == 1 ? c->canary : *old, which == 0 ? *old : c->canary);
    }
    read_full_state(r);
    return c->read();
}

// For a mixed result, which outputs saw what: RGB0-2 as R,G,B, MAC0-3,
// IR0-3, SXY0-2 as X,Y, SZ0-3, FLAG. O / N / X per component, '.' where the
// two references agree.
static char comp(uint32_t o, uint32_t n, uint32_t a, int shift, uint32_t m) {
    o = (o >> shift) & m;
    n = (n >> shift) & m;
    a = (a >> shift) & m;
    return o == n ? '.' : a == o ? 'O' : a == n ? 'N' : 'X';
}

static void printFields(const GwuCell* c, int n, int off, const probe_result_t* o, const probe_result_t* nw,
                        const probe_result_t* a) {
    const uint32_t* po = (const uint32_t*)o;
    const uint32_t* pn = (const uint32_t*)nw;
    const uint32_t* pa = (const uint32_t*)a;
    char f[40];
    int k = 0;
    for (int i = 0; i < 19; i++) {
        if (i < 3) {
            for (int b = 0; b < 3; b++) f[k++] = comp(po[i], pn[i], pa[i], b * 8, 0xff);
        } else if (i >= 11 && i < 14) {
            for (int b = 0; b < 2; b++) f[k++] = comp(po[i], pn[i], pa[i], b * 16, 0xffff);
        } else {
            f[k++] = comp(po[i], pn[i], pa[i], 0, 0xffffffffu);
        }
        if (i == 2 || i == 6 || i == 10 || i == 13 || i == 17) f[k++] = ' ';
    }
    f[k] = 0;
    ramsyscall_printf("GWU-FIELDS %s N=%d A%d %s\n", c->label, n, off, f);
}

// O / N: the command's outputs match the old / new reference.
// o / n: same, once the written register is left out, for a register the
// command also writes (the write landed after the command's own result).
// B: the two references agree, so the cell cannot tell. X: neither.
static char classify(const GwuCell* c, int n, int a, int uncached) {
    probe_result_t o, nw, r;
    uint32_t oldv, ignored;
    uint32_t opost = run(c, n, a, uncached, 0, &o, &oldv);
    uint32_t npost = run(c, n, a, uncached, 1, &nw, &ignored);
    run(c, n, a, uncached, 2, &r, &ignored);
    int kept = !c->ctrl && !(opost == oldv && npost == c->canary);
    if (!kept) {
        mask(&o, c);
        mask(&nw, c);
        mask(&r, c);
    }
    if (results_equal(&o, &nw)) return 'B';
    if (results_equal(&r, &o)) return 'O';
    if (results_equal(&r, &nw)) return 'N';
    if (kept) {
        probe_result_t mo = o, mn = nw, mr = r;
        mask(&mo, c);
        mask(&mn, c);
        mask(&mr, c);
        if (!results_equal(&mo, &mn)) {
            if (results_equal(&mr, &mo)) return 'o';
            if (results_equal(&mr, &mn)) return 'n';
        }
    }
    if (!uncached) printFields(c, n, a, &o, &nw, &r);
    return 'X';
}

int main(void) {
    gte_enable();
    uint32_t sr = irq_disable();
    ramsyscall_printf("GWU-START\n");
    int cells = 0, blind = 0, mismatches = 0, moved = 0, coarse = 0;
    for (unsigned fam = 0; fam < sizeof(s_families) / sizeof(s_families[0]); fam++) {
        for (int i = 0; i < *s_families[fam].count; i++) {
            const GwuCell* c = &s_families[fam].cells[i];
            char res[GWU_AMAX + 1][GWU_NMAX + 1];
            for (int a = 0; a <= GWU_AMAX; a++) {
                int uncached = a == GWU_AMAX;
                for (int n = 0; n < GWU_NMAX; n++) res[a][n] = classify(c, n, uncached ? 0 : a, uncached);
                res[a][GWU_NMAX] = 0;
            }
            cells++;
            if (res[0][GWU_NMAX - 1] == 'B') blind++;
            for (int n = 0; n < GWU_NMAX; n++) {
                int same = 1;
                for (int a = 1; a < GWU_AMAX; a++) same &= res[a][n] == res[0][n];
                if (!same) moved++;
                if (c->expect[n] == '?' || (same && res[0][n] == c->expect[n])) continue;
                mismatches++;
                if (c->expect[n] != 'X') coarse++;
                ramsyscall_printf("GWU-MISMATCH %s N=%d expected %c got %c%c%c%c\n", c->label, n, c->expect[n],
                                  res[0][n], res[1][n], res[2][n], res[3][n]);
            }
            ramsyscall_printf("GWU %s = %s %s %s %s U %s\n", c->label, res[0], res[1], res[2], res[3], res[4]);
        }
    }
    ramsyscall_printf("GWU-DONE cells=%d blind=%d moved=%d\n", cells, blind, moved);
    irq_restore(sr);
    ramsyscall_printf("GWU-COARSE-%s mismatches=%d\n", coarse ? "FAIL" : "PASS", coarse);
    ramsyscall_printf("GWU-%s mismatches=%d\n", mismatches ? "FAIL" : "PASS", mismatches);
    // 0: every checked letter matches. 2: only X letters differ (a model without per-term read
    // timing). 1: anything else.
    if (!mismatches) return 0;
    return coarse ? 1 : 2;
}
