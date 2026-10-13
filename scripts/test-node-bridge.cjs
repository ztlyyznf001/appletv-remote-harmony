/* Host Node-API verification only; NOT an ArkTS/Harmony NAPI build. */
'use strict';
const assert = require('node:assert/strict');
const net = require('node:net');
const path = require('node:path');
const { once } = require('node:events');
const native = require(path.resolve(process.argv[2]));

async function rejected(fn, name) {
  await assert.rejects(Promise.resolve().then(fn), e => {
    assert.ok(e instanceof Error, name + ': Error instance');
    assert.ok(typeof e.code === 'string' && e.code.length > 0, name + ': stable error code');
    return true;
  });
  console.log('PASS bridge: ' + name);
}

(async () => {
  assert.equal(typeof native.version(), 'string');
  for (const method of ['beginPairing', 'finishPairing', 'connect', 'press', 'disconnect', 'cancel']) {
    assert.equal(typeof native[method], 'function', method);
  }
  console.log('PASS bridge: expected exports');
  await rejected(() => native.beginPairing('127.0.0.1', 0), 'invalid port');
  await rejected(() => native.connect('127.0.0.1', 12345, new ArrayBuffer(0)), 'invalid credentials');
  await rejected(() => native.finishPairing('abcd'), 'invalid PIN');
  await rejected(() => native.press(14), 'press requires verified session');

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
    const outcome = native.beginPairing('127.0.0.1', server.address().port)
      .then(() => ({ ok: true }), error => ({ ok: false, error }));
    await Promise.race([connected, new Promise((_, reject) => setTimeout(() => reject(new Error('native connect not started')), 3000))]);
    let timerRan = false;
    await new Promise(resolve => setTimeout(() => { timerRan = true; resolve(); }, 20));
    assert.equal(timerRan, true);
    const start = Date.now();
    native.cancel();
    const result = await Promise.race([outcome, new Promise((_, reject) => setTimeout(() => reject(new Error('native cancel did not wake I/O')), 2000))]);
    assert.equal(result.ok, false);
    assert.equal(result.error.code, 'CANCELLED');
    assert.ok(Date.now() - start < 2000);
    console.log('PASS bridge: async I/O leaves event loop free; cancel wakes blocked read');

    const queued = native.beginPairing('127.0.0.1', server.address().port)
      .then(() => ({ ok: true }), error => ({ ok: false, error }));
    native.cancel();
    const stale = await queued;
    assert.equal(stale.ok, false);
    assert.equal(stale.error.code, 'CANCELLED');
    console.log('PASS bridge: cancel invalidates queued work');
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
