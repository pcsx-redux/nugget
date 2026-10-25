'use strict';

// Runs the hardware test suite on the PS1 farm and grades it for the
// hwfarm/ps1 commit status. See .github/workflows/hwfarm.yml.
//
//   node .github/scripts/hwfarm.js run [--out results.json] [name...]
//
// Run from the repository root, after `make -C tests all` (no PCSX_TESTS: the
// hardware runs every case, including the ones gated off for the emulator).
// HWTEST_TOKEN has to be set. With names, only those suites are submitted.
//
// The farm is a post-merge lane, never a gate. All six consoles share one
// runner, and on its incident paths (heartbeat_timeout, lease_timeout,
// runner_lost) the coordinator synthesizes the result envelope, console
// revision included, from the request. A sick farm and a regression must not
// read the same, so every infrastructure outcome grades as "error", and
// "failure" is reserved for a verdict the test program itself produced.

const { execFileSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const CLI = ['-y', '@consoledev/hwtest@0.1.0'];
// Two consoles carry this revision (#3 and #4), so a requeue after an infra
// failure still has somewhere to land, and neither is the flaky PAL unit.
const REVISION = 'SCPH-5501';
const RUN_SECONDS = 300;
const TOTAL_SECONDS = 3600;
const POLL_MS = 10000;

// name;ps-exe under tests/[;feature]. Each one ends on the exit break from
// tests/support/runtime.c, which the farm reads back as the verdict. A feature
// restricts the test to the consoles advertising it, through its own job file.
const TESTS = [
    'basic;basic/basic',
    'cop0;cop0/cop0',
    'cpu;cpu/cpu',
    'dma;dma/dma',
    'gpu-command;gpu-command/gpu-command',
    'gpu-async-stress;gpu-async-stress/gpu-async-stress',
    'gpu-dfe-480i;gpu-dfe-480i/gpu-dfe-480i',
    'gpu-dma-dreq;gpu-dma-dreq/gpu-dma-dreq',
    'gpu-dma-loop;gpu-dma-loop/gpu-dma-loop',
    'gpu-dma-ring;gpu-dma-ring/gpu-dma-ring',
    'gpu-timing;gpu-timing/gpu-timing',
    'gpu-fbrect;gpu-fbrect/gpu-fbrect',
    'gpu-irq;gpu-irq/gpu-irq',
    'gpu-polyline;gpu-polyline/gpu-polyline',
    'gpustat-480i;gpustat-480i/gpustat-480i',
    'gpu-vram-move;gpu-vram-move/gpu-vram-move',
    'gte;gte/gte',
    'gte-latency;gte-latency/gte-latency',
    'gte-latency-color;gte-latency-color/gte-latency-color',
    'gte-latency-dpc;gte-latency-dpc/gte-latency-dpc',
    'gte-latency-lzcs;gte-latency-lzcs/gte-latency-lzcs',
    'gte-latency-math;gte-latency-math/gte-latency-math',
    'gte-latency-misc;gte-latency-misc/gte-latency-misc',
    'gte-latency-mvmva;gte-latency-mvmva/gte-latency-mvmva',
    'gte-latency-perspective;gte-latency-perspective/gte-latency-perspective',
    'gte-latency-singles;gte-latency-singles/gte-latency-singles',
    'gte-write-to-use;gte-write-to-use/gte-write-to-use',
    'icache;icache/icache',
    'irq-branch-slot;irq-branch-slot/irq-branch-slot',
    'libc;libc/libc',
    'load-timings;load-timings/load-timings',
    'memcpy;memcpy/memcpy',
    'memops-unroll;memops-unroll/memops-unroll',
    'memset;memset/memset',
    'modplayer-state;modplayer-state/modplayer-state',
    'psyqo-480i-flip;psyqo-480i-flip/flip480i',
    'psyqo-reinit;psyqo-reinit/reinit',
    'psyqo-spudma;psyqo-spudma/spudma',
    'spu;spu/spu',
    'spu-endmute;spu-endmute/spu-endmute',
    'spu-endredirect;spu-endredirect/spu-endredirect',
    'spu-endx;spu-endx/spu-endx',
    'spu-irqrefire;spu-irqrefire/spu-irqrefire',
    'spu-konlatch;spu-konlatch/spu-konlatch',
    'spu-offvoice;spu-offvoice/spu-offvoice',
    'spu-volsweep;spu-volsweep/spu-volsweep',
    'shell-480i-wait;shell-480i-wait/shell-480i-wait',
    'sio-reset;sio-reset/sio-reset',
    'sr-write;sr-write/sr-write',
    'timers;timers/timers',
    // Each card test erases the card in one fixed slot, so it only runs on a
    // console whose card in that slot is a test card. Another console's slot 1
    // holds a PocketStation.
    'memcard;memcard/memcard;memcard-slot1',
    'memcard-slot2;memcard-slot2/memcard-slot2;memcard-slot2',
    'memcard-bios;memcard-bios/memcard-bios;memcard-slot1',
    'memcard-bios-slot2;memcard-bios-slot2/memcard-bios-slot2;memcard-slot2',
];
for (let p = 1; p <= 23; p++) TESTS.push(`gpu-raster-phase${p};gpu-raster-phase${p}/gpu-raster-phase${p}`);

// ps-exe|reason. As in run-emulator.sh, a ps-exe in neither list fails the
// run, so a new test cannot be left out by accident.
const SKIPS = [
    'bsdec/bsdec|needs a captured bs-in.bin staged beside it',
    'bcc-bits/bcc-bits|probe: prints timings, no verdict',
    'cdrom/cd-cmd-latency/cd-cmd-latency|probe: prints timings, no verdict',
    'cdrom/cdrom|needs the disc from cdrom/create-test-iso.lua',
    'cop-branch/cop-branch|no hardware baseline recorded yet',
    'cop0-sr/cop0-sr|probe: prints what each SR bit does, no verdict, ends in the ROM through BEV',
    'dcache/dcache|no hardware baseline recorded yet',
    'dma-modes/dma-modes|probe: loops forever, no verdict',
    'dma-priority/dma-priority|probe: loops forever, no verdict',
    'spu-wide-writes/spu-wide-writes|probe: loops forever, no verdict',
    'spu-upload-tail/spu-upload-tail|probe: prints per-arm tail loss across wait strategies, verdict only on its own controls',
    'modplayer-upload-tail/modplayer-upload-tail|probe: prints per-arm tail loss across wait strategies, verdict only on its own controls',
    'gpu/gpu|probe: loops forever, no verdict',
    'gpu-nop/gpu-nop|probe: loops forever, no verdict',
    'gpu-fifo/gpu-fifo|probe: prints FIFO readings, no verdict',
    'gte-math-bench/gte-math-bench|benchmark: prints timings, no verdict',
    'mult-timing/mult-timing|probe: prints timings, no verdict',
    'gte-timing/gte-timing|probe: prints timings, no verdict',
    'pocketstation-memmap/pocketstation-memmap|needs the PocketStation console, which the pin excludes',
    'regwrites/regwrites|probe: loops forever, no verdict',
    'rumble/rumble|needs a DualShock and someone to feel it',
    'msan/msan|exercises the emulator memory sanitizer',
    'msan-trip/msan-trip|exercises the emulator memory sanitizer',
    'alloc-tracker/alloc-tracker|exercises the emulator allocation tracker, needs -debugger',
    'loadexe/loadexe|covers the emulator -loadexe path on the dynarec (pcsx-redux#2443), nothing to measure on silicon',
    'pcdrv/pcdrv|needs a pcdrv host directory',
    'pcdrv-net/pcdrv-net|needs a pcdrv host and a TCP echo peer',
    'pcdrv-net-off/pcdrv-net-off|needs a pcdrv host, checks the emulator with -pcdrvnet off',
    'psyqo/psyqo-tests|ends on pcsx_exit only, never halts on hardware',
    'psyqo-dmachain/dmachain|ends on pcsx_exit only, never halts on hardware',
    'autoinc/autoinc|probe: prints what Delay/Size bit 13 does to split accesses, no verdict',
    'bcc-narrow/bcc-narrow|probe: prints BCC narrow-read results, no verdict',
    'dma-chopping/dma-chopping|probe: prints CPU progress during a chopped DMA2 transfer, no verdict',
    'exp2-width/exp2-width|probe: prints every access width to the EXP2 region, no verdict',
    'gpu-info8/gpu-info8|probe: prints GP1(10h) index 08h readings, no verdict',
    'mdec-roundtrip/mdec-roundtrip|needs a job file over pcdrv, no verdict',
    'scanline-timing/scanline-timing|probe: prints scanline and frame lengths, loops forever, no verdict',
    'spu-capring/spu-capring|probe: prints capture ring readings, no verdict',
    'spu-envx/spu-envx|probe: prints ENVX readings beside the expected values, always exits 0',
    'spu-revread/revread|graded on the digital audio tap, loops forever, no verdict',
    'spu-revstream/revstream|reverb stimulus for the digital audio tap: loops forever, no verdict',
];
for (const d of ['bank-probe', 'display-area-y', 'drawing-area-y', 'drawing-offset-y', 'fast-fill-h-quirk', 'fast-fill-y',
                 'gp1-09-matrix', 'primitives-cross', 'transfer-h-quirk', 'vram-blit-y', 'vram-transfers-y']) {
    SKIPS.push(`2mb-vram/${d}/${d}|probe for 2MB-VRAM hardware: loops forever, no verdict`);
}
for (const d of ['gate-flip-queued', 'texpage-upper', 'transfer-wrap-y']) {
    SKIPS.push(`2mb-vram/${d}/${d}|needs 2MB-VRAM hardware, the pinned consoles have 1MB`);
}

// Ticket failure codes that say nothing about the program. The lower-case
// ones are the coordinator's own (scheduler.ts); RUNNER_LOST and ABORTED are
// verdicts a failure can be derived from.
const INFRA = new Map([
    ['heartbeat_timeout', 'runner heartbeat lost'],
    ['lease_timeout', 'lease hard deadline expired'],
    ['runner_lost', 'runner lost'],
    ['RUNNER_LOST', 'runner lost'],
    ['queue_timeout', 'never got a console'],
    ['cancelled', 'ticket cancelled'],
    ['stop_requested', 'operator stopped the run'],
    ['aborted', 'run aborted'],
    ['ABORTED', 'run aborted'],
]);

function warning(envelope, code) {
    return ((envelope && envelope.warnings) || []).find((w) => w.code === code);
}

// One ticket, terminal or not, to {kind, state, detail}. kind is pass, test
// or infra; state is the commit status it maps to. Anything not positively
// recognised as the program's own verdict is infra.
function classify(ticket, envelopes) {
    const envelope = (envelopes || []).slice(-1)[0];
    const infra = (detail) => ({ kind: 'infra', state: 'error', detail });
    const test = (detail) => ({ kind: 'test', state: 'failure', detail });
    if (!ticket || !ticket.state) return infra('ticket unreadable');
    if (ticket.state === 'DONE') {
        const verdict = envelope && envelope.verdict;
        if (verdict === 'PASS') return { kind: 'pass', state: 'success', detail: 'pass' };
        if (verdict === 'FAIL') {
            const w = warning(envelope, 'NONZERO_EXIT');
            return test(w ? w.message : 'FAIL');
        }
        return infra(`DONE with verdict ${verdict || 'none'}`);
    }
    if (ticket.state !== 'FAILED') return infra(`still ${ticket.state}`);
    // A program that ran to completion and failed can still leave the ticket
    // FAILED. Its own FAIL verdict is the answer, whatever the ticket says.
    if (envelope && envelope.verdict === 'FAIL') {
        const w = warning(envelope, 'NONZERO_EXIT');
        return test(w ? w.message : 'FAIL');
    }
    const failure = ticket.failure;
    // A runner that throws mid-dispatch reports RUNNER_ERROR as a state
    // update, which leaves the ticket FAILED with no failure attached.
    if (!failure || !failure.code) return infra('runner error');
    if (INFRA.has(failure.code)) return infra(`${failure.code}: ${INFRA.get(failure.code)}`);
    // Upper case is the runner's verdict for the program (unirom-driver.ts).
    if (failure.code === 'TIMEOUT' && warning(envelope, 'NO_HALT')) return test('never reached its exit break');
    if (failure.code === 'ERROR') {
        const stopped = warning(envelope, 'PROGRAM_STOPPED');
        if (stopped) return test(stopped.message);
        if (warning(envelope, 'EXIT_CODE_UNREAD')) return infra('halted, exit code unread');
    }
    return infra(`unclassified ${failure.code}`);
}

// All tickets to the one commit status. A program's own failure outranks
// infra, but infra never turns into a failure: with no failing test and any
// infra outcome, the status is error.
function summarize(results) {
    const count = (k) => results.filter((r) => r.kind === k);
    const fails = count('test');
    const infra = count('infra');
    const passed = count('pass').length;
    const names = (list) => list.map((r) => r.name).join(' ');
    let state = 'success';
    let description = `${passed}/${results.length} passed on ${REVISION}`;
    if (fails.length) {
        state = 'failure';
        description = `${fails.length} failed: ${names(fails)}`;
    } else if (infra.length) {
        state = 'error';
        // Count each distinct cause: one stuck ticket must not label the rest.
        const causes = new Map();
        for (const r of infra) {
            const cause = r.detail.split(':')[0];
            causes.set(cause, (causes.get(cause) || 0) + 1);
        }
        const summary = [...causes].map(([c, n]) => `${c} x${n}`).join(', ');
        description = `INFRASTRUCTURE (${infra.length}): ${summary}`;
    }
    if (results.length === 0) {
        state = 'error';
        description = 'nothing was submitted';
    }
    if (description.length > 140) description = description.slice(0, 137) + '...';
    return { state, description };
}

function hwtest(args) {
    const out = execFileSync('npx', [...CLI, ...args, '--json'], {
        encoding: 'utf8',
        stdio: ['ignore', 'pipe', 'inherit'],
        maxBuffer: 64 * 1024 * 1024,
    });
    return JSON.parse(out);
}

function listed(root) {
    const problems = [];
    const known = new Set([...TESTS.map((t) => t.split(';')[1]), ...SKIPS.map((s) => s.split('|')[0])]);
    const found = [];
    const walk = (dir) => {
        for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
            const p = path.join(dir, e.name);
            if (e.isDirectory()) walk(p);
            else if (e.name.endsWith('.ps-exe')) found.push(path.relative(root, p).replace(/\.ps-exe$/, ''));
        }
    };
    walk(root);
    for (const f of found) if (!known.has(f)) problems.push(`UNLISTED: tests/${f}.ps-exe is neither run nor skipped`);
    for (const k of known) if (!found.includes(k)) problems.push(`MISSING: tests/${k}.ps-exe was not built`);
    return problems;
}

// The manifest gate's problems to a results file. A refusal is neither a
// regression nor a sick farm: nothing was submitted, and the fix is a line in
// TESTS or SKIPS above.
function refusal(problems) {
    const names = (tag) => problems.filter((p) => p.startsWith(`${tag}: `)).map((p) => p.split(' ')[1]);
    const unlisted = names('UNLISTED');
    const missing = names('MISSING');
    const parts = [];
    if (unlisted.length) parts.push(`${unlisted.length} unlisted`);
    if (missing.length) parts.push(`${missing.length} not built`);
    const description = `MANIFEST REFUSED: ${parts.join(', ')}, see hwfarm.js TESTS/SKIPS`;
    return { summary: { state: 'error', description }, refused: { unlisted, missing }, results: [] };
}

async function run(argv) {
    let out = 'hwfarm-results.json';
    const only = [];
    for (let i = 0; i < argv.length; i++) {
        if (argv[i] === '--out') out = argv[++i];
        else only.push(argv[i]);
    }
    const problems = only.length ? [] : listed('tests');
    problems.forEach((p) => console.log(p));
    if (problems.length) {
        // Exit 2 fails the job. The results file carries the refused names so
        // the workflow can open an issue and the status can say what happened.
        fs.writeFileSync(out, JSON.stringify(refusal(problems), null, 2));
        process.exit(2);
    }

    const tests = TESTS.map((t) => t.split(';')).filter(([name]) => !only.length || only.includes(name));
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hwfarm-'));
    // The feature sits in the job file beside the revision, so the whole
    // capabilityRequest is in one place.
    const jobFile = (feature) => {
        const file = path.join(tmp, feature ? `job-${feature}.json` : 'job.json');
        const capabilityRequest = { consoleRevisions: [REVISION] };
        if (feature) capabilityRequest.requiredFeatures = [feature];
        fs.writeFileSync(file, JSON.stringify({ capabilityRequest }));
        return file;
    };
    const key = `${process.env.GITHUB_SHA || 'local'}-${process.env.GITHUB_RUN_ID || Date.now()}-${process.env.GITHUB_RUN_ATTEMPT || 1}`;

    const results = [];
    for (const [name, exe, feature] of tests) {
        const args = ['submit', '--exe', `tests/${exe}.ps-exe`, '--manifest', jobFile(feature), '--pool', 'ps1',
                      '--exclude-feature', 'flaky', '--priority', 'CI', '--run-seconds', String(RUN_SECONDS),
                      '--total-seconds', String(TOTAL_SECONDS), '--idempotency-key', `${key}-${name}`];
        console.log(`hwtest ${args.join(' ')}`);
        try {
            const submitted = hwtest(args);
            const required = ((submitted.jobSpec || {}).capabilityRequest || {}).requiredFeatures || [];
            if (feature && !required.includes(feature)) {
                // Not graded as the test's result: it may be running on the wrong console.
                results.push({ name, ticketId: submitted.ticket.ticketId, kind: 'infra', state: 'error',
                               detail: `feature lost: asked ${feature}, job requires "${required.join(',')}"` });
                continue;
            }
            results.push({ name, ticketId: submitted.ticket.ticketId });
            console.log(`${name}: ${submitted.ticket.ticketId}${feature ? ` (${feature})` : ''}`);
        } catch (e) {
            results.push({ name, kind: 'infra', state: 'error', detail: `submit failed: ${e.message.split('\n')[0]}` });
        }
    }

    // The ticket is the durable handle, not the client: poll its state until
    // terminal. TOTAL_SECONDS bounds it on the farm side (queue_timeout); the
    // extra margin here covers the coordinator's own reaping.
    const deadline = Date.now() + (TOTAL_SECONDS + 600) * 1000;
    for (const r of results) {
        if (!r.ticketId || r.kind) continue;
        let ticket;
        for (;;) {
            try {
                ticket = hwtest(['status', r.ticketId]);
            } catch (e) {
                ticket = undefined;
            }
            if ((ticket && ['DONE', 'FAILED'].includes(ticket.state)) || Date.now() > deadline) break;
            await new Promise((resolve) => setTimeout(resolve, POLL_MS));
        }
        // A terminal ticket can come back with no envelope when the result
        // call fails, and that reads as "verdict none". Ask again before
        // grading it as infra.
        let envelopes = [];
        for (let attempt = 0; attempt < 6 && envelopes.length === 0; attempt++) {
            if (attempt) await new Promise((resolve) => setTimeout(resolve, POLL_MS));
            try {
                envelopes = hwtest(['result', r.ticketId]).resultEnvelopes || [];
            } catch (e) {
                console.log(`${r.name}: result ${r.ticketId} failed: ${e.message.split('\n')[0]}`);
            }
            if (!ticket || !['DONE', 'FAILED'].includes(ticket.state)) break;
        }
        Object.assign(r, classify(ticket, envelopes));
        const last = envelopes[envelopes.length - 1];
        // Only a runner-written envelope measures the console; a synthesized
        // one names the revision that was asked for and runner "unknown".
        if (last && last.runner && last.runner.runnerId !== 'unknown') r.console = last.console && last.console.consoleRevision;
        console.log(`${r.kind.toUpperCase()} ${r.name} ${r.ticketId} ${r.console || '-'}: ${r.detail}`);
    }

    const summary = summarize(results);
    fs.writeFileSync(out, JSON.stringify({ summary, results }, null, 2));
    console.log(`\n${summary.state}: ${summary.description}`);
}

module.exports = { classify, summarize, listed, refusal, TESTS, SKIPS, REVISION };

if (require.main === module) {
    const [cmd, ...rest] = process.argv.slice(2);
    if (cmd !== 'run') {
        console.error('usage: hwfarm.js run [--out results.json] [name...]');
        process.exit(2);
    }
    run(rest).catch((e) => {
        console.error(e);
        process.exit(1);
    });
}
