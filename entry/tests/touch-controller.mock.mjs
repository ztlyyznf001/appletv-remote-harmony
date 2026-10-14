// Exercise actual ArkTS pump/controller methods with deterministic native/timer mocks.
import assert from 'node:assert/strict';
import { TouchGesturePump, TouchSample, deferred, tick, codeError, fixture } from './test-fixtures.mjs';

const B = TouchSample.BEGIN, M = TouchSample.MOVE, H = TouchSample.HOLD, E = TouchSample.END;
const sample = (phase, x = 500, y = 500, tap = false) => new TouchSample(x, y, phase, tap);
const kinds = f => f.events.map(e => e.kind === 'touch' ? e.phase : e.kind === 'hold' ? `hold:${e.held}` : `press:${e.code}`);
async function releaseGate(f, kind) {
  const gate = f.gates[kind]; f.gates[kind] = undefined; gate.resolve(); await tick();
}
async function connectedFixture() { const f = fixture({ saved: true }); await f.connected(); return f; }
let count = 0;
async function check(name, fn) { await fn(); count++; console.log('PASS ' + name); }

await check('pump rejects out-of-order, duplicate BEGIN/HOLD/END and nonfinite samples', async () => {
  const sent = [], errors = [], gate = deferred();
  const pump = new TouchGesturePump(async s => { sent.push(s); await gate.promise; }, e => errors.push(e));
  for (const phase of [M, H, E, 99]) assert.equal(pump.offer(sample(phase)), false);
  assert.equal(pump.offer(sample(B, NaN)), false);
  assert.equal(pump.offer(sample(B, Infinity)), false);
  assert.equal(pump.offer(sample(B)), true);
  assert.equal(pump.offer(sample(B)), false);
  assert.equal(pump.offer(sample(H)), true);
  assert.equal(pump.offer(sample(H)), false);
  assert.equal(pump.offer(sample(E)), true);
  for (const phase of [B, M, H, E]) assert.equal(pump.offer(sample(phase)), false);
  gate.resolve(); await tick();
  assert.deepEqual(sent.map(s => s.phase), [B, H, E]); assert.deepEqual(errors, []);
});

await check('pump coalesces 20,000 moves with fixed bound, preserves HOLD and final endpoints', async () => {
  const gate = deferred(), sent = [];
  let active = 0, max = 0;
  const pump = new TouchGesturePump(async s => { active++; max = Math.max(max, active); sent.push(s); await gate.promise; active--; }, e => { throw e; });
  pump.offer(sample(B, -100, 10000));
  for (let i = 0; i < 10000; i++) { pump.offer(sample(M, i, i)); assert.ok(pump.queue.length <= 1); }
  pump.offer(sample(H));
  for (let i = 0; i < 10000; i++) { pump.offer(sample(M, i / 20, i / 40)); assert.ok(pump.queue.length <= 3); }
  pump.offer(sample(E, 700, 800, true));
  assert.equal(sent.length, 1); assert.ok(pump.queue.length <= 4);
  gate.resolve(); await tick();
  assert.deepEqual(sent.map(s => s.phase), [B, M, H, M, E]);
  assert.deepEqual([sent[0].x, sent[0].y], [0, 1000]);
  assert.deepEqual([sent[1].x, sent[1].y], [1000, 1000]);
  assert.deepEqual([sent[3].x, sent[3].y], [500, 250]);
  assert.deepEqual([sent.at(-1).x, sent.at(-1).y, sent.at(-1).tap], [700, 800, false]);
  assert.equal(max, 1);
});

await check('pump cancel drops unsent HOLD/MOVE and emits one non-tapping END', async () => {
  const gate = deferred(), sent = [];
  const pump = new TouchGesturePump(async s => { sent.push(s); await gate.promise; }, e => { throw e; });
  pump.offer(sample(B, 10, 20)); pump.offer(sample(M, 60, 70)); pump.offer(sample(H));
  pump.cancel(); pump.cancel(); gate.resolve(); await tick();
  assert.deepEqual(sent.map(s => s.phase), [B, E]);
  assert.deepEqual([sent[1].x, sent[1].y, sent[1].tap, sent[1].cancelled], [60, 70, false, true]);
});

await check('pump cancellation revokes terminal sample even while sender awaits END', async () => {
  const gate = deferred(), sent = [];
  const pump = new TouchGesturePump(async s => { sent.push(s); if (s.phase === E) await gate.promise; }, e => { throw e; });
  pump.offer(sample(B)); await tick(); pump.offer(sample(E, 100, 200, true));
  assert.equal(sent.at(-1).tap, true); pump.cancel();
  assert.equal(sent.at(-1).tap, false); assert.equal(sent.at(-1).cancelled, true);
  gate.resolve(); await tick(); assert.equal(sent.length, 2);
});

await check('pump abort drops pending work and ignores late native rejection', async () => {
  const gate = deferred(), sent = [], errors = [];
  const pump = new TouchGesturePump(async s => { sent.push(s); await gate.promise; }, e => errors.push(e));
  pump.offer(sample(B)); pump.offer(sample(M)); pump.offer(sample(E, 1, 1, true)); pump.abort();
  gate.reject(codeError('CANCELLED')); await tick();
  assert.equal(sent.length, 1); assert.deepEqual(errors, []); assert.equal(pump.queue.length, 0);
});

await check('pump transport failure reports once and never replays queued input', async () => {
  const gate = deferred(), sent = [], errors = [];
  const pump = new TouchGesturePump(async s => { sent.push(s); await gate.promise; }, e => errors.push(e));
  pump.offer(sample(B)); pump.offer(sample(M)); pump.offer(sample(E, 1, 1, true));
  gate.reject(codeError('DISCONNECTED')); await tick();
  assert.equal(sent.length, 1); assert.equal(errors.length, 1); assert.equal(pump.queue.length, 0);
  assert.equal(pump.offer(sample(B)), false);
});

await check('controller accepts only foreground connected idle finite touch starts', async () => {
  const f = fixture({ saved: true }); const c = f.controller;
  assert.equal(c.beginTouch(1, 2), false); await f.connected();
  for (const key of ['busy', 'buttonInFlight', 'touchInFlight']) {
    c.state[key] = true; assert.equal(c.beginTouch(1, 2), false); c.state[key] = false;
  }
  for (const [x, y] of [[NaN, 1], [1, Infinity], [-Infinity, 1]]) assert.equal(c.beginTouch(x, y), false);
  assert.equal(c.beginTouch(1, 2), true); assert.equal(c.beginTouch(3, 4), false);
  c.cancelTouch(); await tick(); assert.equal(c.state.touchInFlight, false);
});

await check('controller coalesces slow-native movement without enqueuing native work', async () => {
  const f = await connectedFixture(); f.gates.touch = deferred();
  f.controller.beginTouch(5, 6);
  for (let i = 0; i < 10000; i++) f.controller.moveTouch(i / 10, i / 20);
  f.controller.endTouch(700, 800, false);
  assert.equal(f.calls.touch, 1); await releaseGate(f, 'touch');
  assert.deepEqual(kinds(f), [B, M, E]);
  assert.deepEqual([f.events[1].x, f.events[1].y], [1000, 500]);
  assert.deepEqual([f.events[2].x, f.events[2].y], [700, 800]);
  assert.equal(f.maxActive(), 1); assert.equal(f.controller.state.touchInFlight, false);
});

await check('tap waits for reliable END and SELECT completion while competing keys stay locked', async () => {
  const f = await connectedFixture(); const c = f.controller;
  c.beginTouch(100, 200); await tick(); f.gates.touch = deferred(); f.gates.press = deferred();
  c.endTouch(110, 220, true); c.endTouch(110, 220, true); c.moveTouch(999, 999); c.holdTouch();
  await c.press(1); assert.deepEqual(kinds(f), [B, E]); assert.equal(c.state.touchInFlight, true);
  await releaseGate(f, 'touch'); assert.deepEqual(kinds(f), [B, E, 'press:6']);
  assert.equal(c.beginTouch(1, 2), false); await c.press(7); assert.equal(f.calls.press, 1);
  await releaseGate(f, 'press'); assert.equal(c.state.touchInFlight, false);
  await c.press(7); assert.equal(f.calls.press, 2); assert.equal(f.maxActive(), 1);
});

await check('cancel during BEGIN sends END at latest position with no tap or queued held SELECT', async () => {
  const f = await connectedFixture(); f.gates.touch = deferred();
  f.controller.beginTouch(10, 20); f.controller.moveTouch(30, 40); f.controller.holdTouch();
  f.controller.cancelTouch(); f.controller.cancelTouch(); await releaseGate(f, 'touch');
  assert.deepEqual(kinds(f), [B, E]); assert.deepEqual([f.events[1].x, f.events[1].y], [30, 40]);
  assert.equal(f.controller.state.connected, true); assert.equal(f.controller.state.touchInFlight, false);
});

await check('cancel during END prevents queued tap SELECT', async () => {
  const f = await connectedFixture(); f.controller.beginTouch(1, 2); await tick();
  f.gates.touch = deferred(); f.controller.endTouch(3, 4, true); f.controller.cancelTouch();
  await releaseGate(f, 'touch'); assert.deepEqual(kinds(f), [B, E]);
  assert.equal(f.controller.state.touchInFlight, false);
});

await check('long press uses genuine SELECT down/up and a relative one-second timer, never wall time', async () => {
  const f = await connectedFixture(); const c = f.controller;
  c.beginTouch(500, 500); await tick(); c.holdTouch(); c.holdTouch(); await tick();
  assert.deepEqual(kinds(f), [B, 'hold:true']); c.endTouch(500, 500, true); await tick();
  assert.deepEqual(kinds(f), [B, 'hold:true', E]); assert.equal(c.state.touchInFlight, true);
  assert.equal(f.timers.size, 1); const [id, timer] = f.timers.entries().next().value;
  assert.equal(timer.delay, 1000); await f.fireTimer(id);
  assert.deepEqual(kinds(f), [B, 'hold:true', E, 'hold:false']);
  assert.equal(c.state.touchInFlight, false); assert.equal(f.maxActive(), 1);
});

await check('elapsed long press releases without an unnecessary extra delay', async () => {
  const f = await connectedFixture(); f.controller.beginTouch(1, 2); await tick();
  f.controller.holdTouch(); await tick(); await f.advance(1200); f.controller.endTouch(1, 2, false); await tick();
  assert.equal(f.timers.size, 0); assert.deepEqual(kinds(f), [B, 'hold:true', E, 'hold:false']);
});

await check('finger-up after a partial hold waits only for original SELECT-down timer remainder', async () => {
  const f = await connectedFixture(); f.controller.beginTouch(1, 2); await tick();
  f.controller.holdTouch(); await tick(); await f.advance(400);
  f.controller.endTouch(1, 2, false); await tick(); await f.advance(599);
  assert.deepEqual(kinds(f), [B, 'hold:true', E]); assert.equal(f.controller.state.touchInFlight, true);
  await f.advance(1); assert.deepEqual(kinds(f), [B, 'hold:true', E, 'hold:false']);
  assert.equal(f.controller.state.touchInFlight, false);
});

await check('cancellation during minimum-hold timer releases immediately and clears timer', async () => {
  const f = await connectedFixture(); const c = f.controller;
  c.beginTouch(1, 2); await tick(); c.holdTouch(); await tick(); c.endTouch(1, 2, true); await tick();
  assert.equal(f.timers.size, 1); c.cancelTouch(); assert.equal(f.timers.size, 0); await tick();
  assert.deepEqual(kinds(f), [B, 'hold:true', E, 'hold:false']); assert.equal(c.state.touchInFlight, false);
});

await check('cancellation while SELECT-down awaits native still pairs it with SELECT-up', async () => {
  const f = await connectedFixture(); f.controller.beginTouch(1, 2); await tick();
  f.gates.hold = deferred(); f.controller.holdTouch(); f.controller.cancelTouch();
  await releaseGate(f, 'hold'); assert.deepEqual(kinds(f), [B, 'hold:true', E, 'hold:false']);
  assert.equal(f.timers.size, 0); assert.equal(f.controller.state.touchInFlight, false);
});

await check('invalid MOVE/END cancels at last finite coordinates, never creates a tap', async () => {
  for (const method of ['moveTouch', 'endTouch']) {
    const f = await connectedFixture(); f.controller.beginTouch(-10, 2000); await tick();
    f.controller[method](NaN, Infinity, true); await tick();
    assert.deepEqual(kinds(f), [B, E]);
    assert.deepEqual(f.events.map(e => [e.x, e.y]), [[0, 1000], [0, 1000]]);
    assert.equal(f.controller.state.touchInFlight, false);
  }
});

await check('background/disconnect/device switch abort pending gesture and close transport', async () => {
  for (const stop of [c => c.leaveForeground(), c => c.disconnect(), (c, target) => c.select(target)]) {
    const f = await connectedFixture(); const c = f.controller; f.gates.touch = deferred();
    c.beginTouch(1, 2); c.moveTouch(3, 4); c.holdTouch(); c.endTouch(5, 6, true);
    const previousEpoch = c.state.generation; stop(c, f.target);
    assert.ok(c.state.generation > previousEpoch); assert.equal(c.state.touchInFlight, false);
    assert.equal(c.state.connected, false); assert.ok(f.calls.cancel > 0); assert.ok(f.calls.disconnect > 0);
    await releaseGate(f, 'touch'); assert.deepEqual(kinds(f), [B]); assert.equal(f.timers.size, 0);
  }
});

await check('background while hold timer waits cannot release into next session', async () => {
  const f = await connectedFixture(); const c = f.controller;
  c.beginTouch(1, 2); await tick(); c.holdTouch(); await tick(); c.endTouch(1, 2, false); await tick();
  assert.equal(f.timers.size, 1); c.leaveForeground(); assert.equal(f.timers.size, 0);
  c.enterForeground(); await c.connectSelected(); c.beginTouch(300, 400); await tick();
  assert.deepEqual(kinds(f), [B, 'hold:true', E, B]); assert.equal(c.state.touchInFlight, true);
  c.endTouch(300, 400, false); await tick(); assert.deepEqual(kinds(f), [B, 'hold:true', E, B, E]);
  assert.equal(f.events.at(-1).session, 2); assert.equal(c.state.connected, true);
});

await check('late old BEGIN success cannot send stale END or unlock a new gesture', async () => {
  const f = await connectedFixture(); const c = f.controller; const old = deferred(); f.gates.touch = old;
  c.beginTouch(1, 2); c.endTouch(1, 2, true); c.disconnect(); f.gates.touch = undefined;
  await c.connectSelected(); c.beginTouch(700, 800); await tick(); old.resolve(); await tick();
  assert.deepEqual(kinds(f), [B, B]); assert.equal(c.state.touchInFlight, true);
  c.endTouch(700, 800, false); await tick(); assert.deepEqual(kinds(f), [B, B, E]);
  assert.deepEqual(f.events.map(e => e.session), [1, 2, 2]);
});

await check('late old failure cannot close new verified session or overwrite its status', async () => {
  const f = await connectedFixture(); const c = f.controller; const old = deferred(); f.gates.touch = old;
  c.beginTouch(1, 2); c.disconnect(); f.gates.touch = undefined; await c.connectSelected();
  const cancels = f.calls.cancel, status = c.state.status; old.reject(codeError('AUTH_FAILED')); await tick();
  assert.equal(c.state.connected, true); assert.equal(c.state.status, status); assert.equal(f.calls.cancel, cancels);
});

await check('stale dispatched hold timer cannot clear or resolve newer session hold timer', async () => {
  const f = await connectedFixture(); const c = f.controller;
  c.beginTouch(1, 2); await tick(); c.holdTouch(); await tick();
  const staleCallback = f.timers.values().next().value.fn;
  c.disconnect(); await c.connectSelected(); c.beginTouch(3, 4); await tick(); c.holdTouch(); await tick();
  const [id, timer] = f.timers.entries().next().value;
  staleCallback(); c.endTouch(3, 4, false); await tick();
  assert.equal(f.timers.size, 1); assert.equal(f.timers.get(id), timer); assert.equal(c.state.touchInFlight, true);
  assert.deepEqual(kinds(f), [B, 'hold:true', B, 'hold:true', E]);
  await f.fireTimer(id); assert.deepEqual(kinds(f), [B, 'hold:true', B, 'hold:true', E, 'hold:false']);
});

await check('touch authentication/identity/protocol/network errors close safely without retry or raw error text', async () => {
  for (const code of ['AUTH_FAILED', 'IDENTITY_CHANGED', 'PROTOCOL_ERROR', 'TIMEOUT', 'DISCONNECTED', 'NOT_CONNECTED']) {
    const f = await connectedFixture(); f.failNext('touch', code);
    f.controller.beginTouch(1, 2); f.controller.moveTouch(2, 3); f.controller.endTouch(2, 3, true); await tick();
    assert.deepEqual(kinds(f), [B]); assert.equal(f.controller.state.connected, false);
    assert.equal(f.controller.state.touchInFlight, false); assert.ok(f.calls.cancel > 0);
    assert.equal(f.timers.size, 0); assert.equal(f.calls.connect, 1);
    assert.equal(f.controller.state.status.includes('DO NOT DISPLAY'), false);
  }
});

await check('END, SELECT-down, SELECT-up and tap failures never replay subsequent inputs', async () => {
  for (const point of ['end', 'down', 'up', 'tap']) {
    const f = await connectedFixture(); const c = f.controller; c.beginTouch(1, 2); await tick();
    if (point === 'down') { f.failNext('hold', 'DISCONNECTED'); c.holdTouch(); c.endTouch(1, 2, false); }
    else if (point === 'up') { c.holdTouch(); await tick(); f.failNext('hold', 'DISCONNECTED'); c.cancelTouch(); }
    else { f.failNext(point === 'end' ? 'touch' : 'press', 'DISCONNECTED'); c.endTouch(1, 2, true); }
    await tick(); assert.equal(c.state.connected, false); assert.equal(c.state.touchInFlight, false);
    assert.equal(f.timers.size, 0); assert.ok(f.calls.cancel > 0);
    if (point !== 'tap') assert.equal(f.calls.press, 0);
  }
});

await check('rapid completed/cancelled gestures do not leak state or emit extra taps', async () => {
  const f = await connectedFixture(); const c = f.controller;
  for (let i = 0; i < 20; i++) {
    assert.equal(c.beginTouch(i, i), true);
    if (i % 2) c.cancelTouch(); else c.endTouch(i, i, true);
    await tick(); assert.equal(c.state.touchInFlight, false); assert.equal(f.timers.size, 0);
  }
  assert.equal(f.calls.touch, 40); assert.equal(f.calls.press, 10); assert.equal(f.calls.hold, 0);
  assert.equal(f.maxActive(), 1); assert.equal(c.state.connected, true);
});

console.log(`${count} touch pump/controller checks passed. NOT ArkTS compilation or device validation.`);
