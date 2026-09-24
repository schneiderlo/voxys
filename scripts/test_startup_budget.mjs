import assert from 'node:assert/strict';
import {test} from 'node:test';
import {startupBudget} from './startup_budget.mjs';

test('software GPU startup covers the page deadline plus rendered-frame validation', () => {
    for (const mode of ['swiftshader', 'swiftshader-window', 'swiftshader-legacy']) {
        const {startupMs, totalMs} = startupBudget({VOXY_SMOKE_GPU: mode});
        assert(startupMs >= 600000 + 60000);
        assert(totalMs >= startupMs + 60000);
    }
});

test('hardware checks retain their existing budgets', () => {
    assert.deepEqual(startupBudget({}), {startupMs: 180000, totalMs: 240000});
});

test('a global override cannot silently cut startup short', () => {
    assert.throws(() => startupBudget({VOXY_SMOKE_GPU: 'swiftshader-window',
        VOXY_SMOKE_TIMEOUT_MS: '480000'}), /at least 60 seconds/);
    assert.equal(startupBudget({VOXY_SMOKE_GPU: 'swiftshader-window',
        VOXY_SMOKE_TIMEOUT_MS: '900000'}).totalMs, 900000);
});

test('invalid or unbounded overrides are rejected', () => {
    for (const value of ['NaN', 'Infinity', '-1', '240000.5', '1800001']) {
        assert.throws(() => startupBudget({VOXY_SMOKE_TIMEOUT_MS: value}));
    }
});
