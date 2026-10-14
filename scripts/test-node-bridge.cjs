/* Host Node-API verification only; NOT an ArkTS/Harmony NAPI build. */
'use strict';
const assert = require('node:assert/strict');
const net = require('node:net');
const path = require('node:path');
const { once } = require('node:events');
const native = require(path.resolve(process.argv[2]));

async function rejected(fn, name, code) {
  const result = fn();
  assert.ok(result instanceof Promise, name + ': Promise result');
  await assert.rejects(result, e => {
    assert.ok(e instanceof Error, name + ': Error instance');
    assert.ok(typeof e.code === 'string' && e.code.length > 0, name + ': stable error code');
    if (code) assert.equal(e.code, code, name + ': expected error code');
    return true;
  });
  console.log('PASS bridge: ' + name);
}
async function bounded(promise, ms, message) {
  let watchdog;
  try {
    return await Promise.race([promise, new Promise((_, reject) => {
      watchdog = setTimeout(() => reject(new Error(message)), ms);
    })]);
  } finally { clearTimeout(watchdog); }
}
function outcome(promise) {
  return promise.then(() => ({ ok: true }), error => ({ ok: false, error }));
}

(async () => {
  assert.equal(typeof native.version(), 'string');
  for (const method of ['beginPairing', 'finishPairing', 'connect', 'press', 'touch', 'holdSelect', 'disconnect', 'cancel']) {
    assert.equal(typeof native[method], 'function', method);
  }
  console.log('PASS bridge: expected exports');
  await rejected(() => native.beginPairing('127.0.0.1', 0), 'invalid port', 'INVALID_INPUT');
  await rejected(() => native.connect('127.0.0.1', 12345, new ArrayBuffer(0)), 'invalid credentials', 'INVALID_INPUT');
  await rejected(() => native.finishPairing('abcd'), 'invalid PIN', 'INVALID_INPUT');
  await rejected(() => native.press(14), 'press requires verified session', 'NOT_CONNECTED');

  // Node-API must reject rather than coerce floats, non-finite numbers or other JS types.
  const invalidCoordinates = [-1, 1001, 0.5, NaN, Infinity, -Infinity, '500', true, null,
    undefined, {}, [], new Number(500), 500n, Symbol('coordinate')];
  for (const [index, value] of invalidCoordinates.entries()) {
    await rejected(() => native.touch(value, 500, 1), `invalid touch x #${index}`, 'INVALID_INPUT');
    await rejected(() => native.touch(500, value, 1), `invalid touch y #${index}`, 'INVALID_INPUT');
  }
  const invalidPhases = [-1, 0, 2, 5, 1.5, NaN, Infinity, -Infinity, '1', true, null,
    undefined, {}, [], new Number(1), 1n, Symbol('phase')];
  for (const [index, value] of invalidPhases.entries()) {
    await rejected(() => native.touch(500, 500, value), `invalid touch phase #${index}`, 'INVALID_INPUT');
  }
  for (const args of [[], [500], [500, 500], [500, 500, 1, 0], Array(10).fill(1)]) {
    await rejected(() => native.touch(...args), `touch arity ${args.length}`, 'INVALID_INPUT');
  }
  const invalidHeld = [0, 1, -1, 0.5, NaN, Infinity, 'true', 'false', null, undefined,
    {}, [], new Boolean(true), 1n, Symbol('held')];
  for (const [index, value] of invalidHeld.entries()) {
    await rejected(() => native.holdSelect(value), `invalid holdSelect argument #${index}`, 'INVALID_INPUT');
  }
  for (const args of [[], [true, false], Array(10).fill(true)]) {
    await rejected(() => native.holdSelect(...args), `holdSelect arity ${args.length}`, 'INVALID_INPUT');
  }
  for (const phase of [1, 3, 4]) {
    for (const [x, y] of [[0, 0], [0, 1000], [1000, 0], [1000, 1000], [500, 500]]) {
      await rejected(() => native.touch(x, y, phase), `touch ${x},${y},${phase} requires verified session`, 'NOT_CONNECTED');
    }
  }
  for (const held of [true, false]) {
    await rejected(() => native.holdSelect(held), `holdSelect ${held} requires verified session`, 'NOT_CONNECTED');
  }

  const peers = new Set();
  const server = net.createServer(socket => {
    peers.add(socket);
    socket.on('close', () => peers.delete(socket));
    socket.on('error', () => {});
    // Deliberately never answer. Tests cancellation rather than a TV simulator.
  });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  try {
    const connected = once(server, 'connection');
    const pairing = outcome(native.beginPairing('127.0.0.1', server.address().port));
    await bounded(connected, 3000, 'native connect not started');
    let timerRan = false;
    await new Promise(resolve => setTimeout(() => { timerRan = true; resolve(); }, 20));
    assert.equal(timerRan, true);
    const start = Date.now();
    native.cancel();
    const result = await bounded(pairing, 2000, 'native cancel did not wake I/O');
    assert.equal(result.ok, false);
    assert.equal(result.error.code, 'CANCELLED');
    assert.ok(Date.now() - start < 2000);
    console.log('PASS bridge: async I/O leaves event loop free; cancel wakes blocked read');

    const queued = outcome(native.beginPairing('127.0.0.1', server.address().port));
    native.cancel();
    const stale = await queued;
    assert.equal(stale.ok, false);
    assert.equal(stale.error.code, 'CANCELLED');
    console.log('PASS bridge: cancel invalidates queued work');

    // Keep one active request blocked so the queue cannot drain while it fills.
    const queueConnection = once(server, 'connection');
    const blocked = outcome(native.beginPairing('127.0.0.1', server.address().port));
    await bounded(queueConnection, 3000, 'queue-test connect not started');
    const settled = [];
    const jobs = Array.from({ length: 64 }, (_, index) => {
      const promise = index % 3 === 0 ? native.touch(index, 1000 - index, 1) :
        index % 3 === 1 ? native.holdSelect(index % 2 === 0) : native.press(6);
      return outcome(promise).then(value => { settled.push(index); return value; });
    });
    await rejected(() => native.touch(500, 500, 3), 'touch respects queue bound', 'BUSY');
    await rejected(() => native.holdSelect(false), 'holdSelect respects queue bound', 'BUSY');
    await rejected(() => native.touch(NaN, 500, 1), 'full queue still validates touch', 'INVALID_INPUT');
    await rejected(() => native.holdSelect(1), 'full queue still validates holdSelect', 'INVALID_INPUT');
    native.cancel();
    const freshEpoch = native.disconnect();
    const [blockedResult, pendingResults] = await bounded(
      Promise.all([blocked, Promise.all(jobs), freshEpoch]), 2000, 'mixed input queue did not cancel');
    assert.equal(blockedResult.error.code, 'CANCELLED');
    for (const pending of pendingResults) {
      assert.equal(pending.ok, false);
      assert.equal(pending.error.code, 'CANCELLED');
    }
    assert.deepEqual(settled, Array.from({ length: 64 }, (_, index) => index));
    console.log('PASS bridge: 64-pending bound, mixed input cancellation and fresh epoch recovery');

    // Closing the fake peer releases the active request without changing the epoch.
    // Every mixed operation must then execute and settle in enqueue order.
    const orderConnection = once(server, 'connection');
    const order = [];
    const record = (name, promise) => outcome(promise).then(value => {
      order.push(name); return value;
    });
    const ordered = [record('pairing', native.beginPairing('127.0.0.1', server.address().port))];
    const [orderPeer] = await bounded(orderConnection, 3000, 'order-test connect not started');
    const operations = [
      ['touch begin', () => native.touch(0, 1000, 1)],
      ['select down', () => native.holdSelect(true)],
      ['press', () => native.press(6)],
      ['touch move', () => native.touch(500, 500, 3)],
      ['select up', () => native.holdSelect(false)],
      ['touch end', () => native.touch(1000, 0, 4)],
      ['disconnect', () => native.disconnect()]
    ];
    for (const [name, operation] of operations) ordered.push(record(name, operation()));
    orderPeer.destroy();
    const orderedResults = await bounded(Promise.all(ordered), 3000, 'mixed input queue did not drain');
    assert.deepEqual(order, ['pairing', ...operations.map(([name]) => name)]);
    assert.equal(orderedResults[0].ok, false);
    for (const pending of orderedResults.slice(1, -1)) assert.equal(pending.error.code, 'NOT_CONNECTED');
    assert.equal(orderedResults.at(-1).ok, true);
    console.log('PASS bridge: touch, held SELECT, press and disconnect preserve FIFO settlement');

    await native.disconnect();
    await native.disconnect();
    console.log('PASS bridge: disconnect is idempotent');
  } finally {
    native.cancel();
    for (const socket of peers) socket.destroy();
    await new Promise(resolve => server.close(resolve));
  }
  console.log('PASS: host Node-API smoke checks; Harmony bridge remains uncompiled.');
})().catch(e => { native.cancel(); console.error(e); process.exitCode = 1; });
