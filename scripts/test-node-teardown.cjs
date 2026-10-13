/* Host-only bounded teardown regression. Explicit native.cancel() is tested separately. */
'use strict';
const assert = require('node:assert/strict');
const { Worker } = require('node:worker_threads');
const net = require('node:net');
const { once } = require('node:events');
const path = require('node:path');

(async () => {
  const peers = new Set();
  const server = net.createServer(socket => {
    peers.add(socket);
    socket.on('error', () => {});
    socket.on('close', () => peers.delete(socket));
  });
  let worker;
  let watchdog;
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  try {
    const accepted = once(server, 'connection');
    worker = new Worker(
      "const { workerData } = require('node:worker_threads'); const remote = require(workerData.addon); remote.beginPairing('127.0.0.1', workerData.port).catch(() => {});",
      { eval: true, workerData: { addon: path.resolve(process.argv[2]), port: server.address().port } }
    );
    const max = new Promise((_, reject) => {
      watchdog = setTimeout(() => reject(new Error('Native worker teardown exceeded 15s watchdog')), 15000);
    });
    await Promise.race([accepted, max]);
    const start = Date.now();
    await Promise.race([worker.terminate(), max]);
    const elapsed = Date.now() - start;
    assert.ok(elapsed < 14000, 'Bounded native timeout must allow worker termination');
    console.log(`PASS host Worker teardown bounded: ${elapsed} ms. Earlier teardown is allowed; immediate cancellation is NOT guaranteed without cancel().`);
  } finally {
    clearTimeout(watchdog);
    for (const socket of peers) socket.destroy();
    if (worker) await worker.terminate();
    await new Promise(resolve => server.close(resolve));
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
