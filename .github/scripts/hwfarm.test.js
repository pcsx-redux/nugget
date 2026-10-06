'use strict';

// node --test .github/scripts/hwfarm.test.js ; run by the rfc-script job in build.yml.
//
// The tickets and envelopes below have the shapes `hwtest status --json` and
// `hwtest result --json` return. The infra ones are what the coordinator
// writes on its own (scheduler.ts): a minimal envelope with runner "unknown"
// and the console revision copied from the request.

const test = require('node:test');
const assert = require('node:assert');
const { classify, summarize } = require('./hwfarm.js');

const done = { state: 'DONE', failure: null };
const failed = (code, message = '') => ({ state: 'FAILED', failure: { code, message, retryable: true } });
const env = (verdict, warnings = [], runnerId = 'runner_906c118d') => ({
    verdict,
    warnings,
    runner: { runnerId },
    console: { consoleRevision: 'SCPH-5501' },
});
const synth = (verdict) => env(verdict, [], 'unknown');

test('exit code 0 passes', () => {
    assert.deepStrictEqual(classify(done, [env('PASS')]), { kind: 'pass', state: 'success', detail: 'pass' });
});

test('a nonzero exit code is a test failure', () => {
    const r = classify(done, [env('FAIL', [{ code: 'NONZERO_EXIT', message: 'program exited with code 1' }])]);
    assert.strictEqual(r.state, 'failure');
    assert.strictEqual(r.detail, 'program exited with code 1');
});

test('a program that never halts is a test failure', () => {
    const r = classify(failed('TIMEOUT'), [env('TIMEOUT', [{ code: 'NO_HALT', message: '' }])]);
    assert.strictEqual(r.state, 'failure');
});

test('a program that crashes is a test failure', () => {
    const r = classify(failed('ERROR'), [env('ERROR', [{ code: 'PROGRAM_STOPPED', message: 'program stopped (AdEL) at 0x80010000' }])]);
    assert.strictEqual(r.state, 'failure');
});

for (const [code, verdict] of [['heartbeat_timeout', 'RUNNER_LOST'], ['lease_timeout', 'TIMEOUT'], ['runner_lost', 'RUNNER_LOST']]) {
    test(`${code} is infrastructure, never failure`, () => {
        const r = classify(failed(code), [synth(verdict)]);
        assert.strictEqual(r.kind, 'infra');
        assert.strictEqual(r.state, 'error');
        assert.match(r.detail, new RegExp(`^${code}`));
    });
}

for (const code of ['RUNNER_LOST', 'queue_timeout', 'cancelled', 'stop_requested', 'aborted', 'ABORTED']) {
    test(`${code} is infrastructure`, () => {
        assert.strictEqual(classify(failed(code), []).state, 'error');
    });
}

test('lease_timeout stays infra even though its verdict reads TIMEOUT', () => {
    // No NO_HALT warning: the runner never reported, nothing ran to a deadline.
    assert.strictEqual(classify(failed('lease_timeout'), [synth('TIMEOUT')]).state, 'error');
    assert.strictEqual(classify(failed('TIMEOUT'), [synth('TIMEOUT')]).state, 'error');
});

test('an unread exit code is infrastructure', () => {
    assert.strictEqual(classify(failed('ERROR'), [env('ERROR', [{ code: 'EXIT_CODE_UNREAD', message: '' }])]).state, 'error');
});

test('a runner error leaves no failure and is infrastructure', () => {
    assert.strictEqual(classify({ state: 'FAILED', failure: null }, []).detail, 'runner error');
});

test('unknown codes, unreadable and non-terminal tickets are never failure', () => {
    assert.strictEqual(classify(failed('something_new'), []).state, 'error');
    assert.strictEqual(classify(undefined, []).state, 'error');
    assert.strictEqual(classify({ state: 'QUEUED' }, []).state, 'error');
    assert.strictEqual(classify(done, []).state, 'error');
});

test('the last attempt decides after a requeue', () => {
    assert.strictEqual(classify(done, [synth('RUNNER_LOST'), env('PASS')]).state, 'success');
});

test('a FAIL verdict on a FAILED ticket is a test failure, not infra', () => {
    const r = classify({ state: 'FAILED' }, [env('FAIL', [{ code: 'NONZERO_EXIT', message: 'program exited with code 1' }])]);
    assert.strictEqual(r.kind, 'test');
    assert.strictEqual(r.state, 'failure');
});

const r = (name, kind) => ({ name, kind, detail: kind === 'infra' ? 'heartbeat_timeout: runner heartbeat lost' : kind });

test('summary: all pass is success', () => {
    assert.strictEqual(summarize([r('a', 'pass'), r('b', 'pass')]).state, 'success');
});

test('summary: infra alone is error, named as infrastructure', () => {
    const s = summarize([r('a', 'pass'), r('b', 'infra')]);
    assert.strictEqual(s.state, 'error');
    assert.match(s.description, /^INFRASTRUCTURE \(1\): heartbeat_timeout x1/);
});

test('summary: infra causes are counted separately', () => {
    const s = summarize([
        { name: 'a', kind: 'infra', state: 'error', detail: 'queue_timeout: never leased' },
        { name: 'b', kind: 'infra', state: 'error', detail: 'queue_timeout: never leased' },
        { name: 'c', kind: 'infra', state: 'error', detail: 'still LEASED' },
    ]);
    assert.strictEqual(s.description, 'INFRASTRUCTURE (3): queue_timeout x2, still LEASED x1');
});

test('summary: a real failure outranks infra', () => {
    assert.strictEqual(summarize([r('a', 'test'), r('b', 'infra')]).state, 'failure');
});

test('summary: nothing submitted is error', () => {
    assert.strictEqual(summarize([]).state, 'error');
});

test('description fits a commit status', () => {
    const many = Array.from({ length: 47 }, (_, i) => r(`gpu-raster-phase${i}`, 'test'));
    assert.ok(summarize(many).description.length <= 140);
});
