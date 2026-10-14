// Node >=24 logic-only harness. This does not compile ArkTS or validate SDK APIs.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import { stripTypeScriptTypes } from 'node:module';

export const ets = relative => stripTypeScriptTypes(fs.readFileSync(new URL('../src/main/ets/' + relative, import.meta.url), 'utf8')
  .replace(/^import .*;\n/gm, '').replace(/^@Observed\n/gm, '').replace(/export default class /g, 'class ').replace(/export /g, ''));
export const Device = new Function(ets('model/Device.ets') + '; return Device;')();
export const manual = new Function(ets('model/Device.ets') + '; return manualEndpoint;')();
export const RemoteState = new Function(ets('model/RemoteState.ets') + '; return RemoteState;')();
export const { TouchGesturePump, TouchSample } = new Function(ets('model/TouchGesturePump.ets') + '; return { TouchGesturePump, TouchSample };')();
const controllerJS = ets('model/RemoteController.ets') + '; return RemoteController;';
export const discoveryJS = ets('discovery/DeviceDiscovery.ets') + '; return DeviceDiscovery;';
export const deferred = () => {
  let resolve, reject;
  const promise = new Promise((r, j) => { resolve = r; reject = j; });
  return { promise, resolve, reject };
};
export const tick = () => new Promise(resolve => setImmediate(resolve));
export const codeError = code => Object.assign(new Error('DO NOT DISPLAY PEER DATA'), { code });

export function fixture({ saved = false, resume = false } = {}) {
  const target = new Device('mdns:TV', 'TV', '192.168.1.2', 12345);
  const data = new Map(saved ? [['last_id', target.id], ['last_name', target.name], ['last_host', target.host], ['last_port', target.port], ['resume_connection', resume]] : []);
  const identity = new Uint8Array([1, 2, 3, 4]);
  const stored = new Map(saved ? [[target.id, identity]] : []);
  const timers = new Map();
  let timerId = 0, now = 100000, session = 0, active = 0, maxActive = 0;
  const endpoints = [], events = [], failures = new Map();
  const calls = { begin: 0, finish: 0, connect: 0, press: 0, touch: 0, hold: 0, cancel: 0, disconnect: 0, save: 0, start: 0, stop: 0 };
  const gates = { begin: undefined, finish: undefined, connect: undefined, press: undefined, touch: undefined, hold: undefined, save: undefined };
  let failure = '';
  async function input(kind, details) {
    calls[kind]++;
    events.push({ kind, ...details, session });
    active++;
    maxActive = Math.max(maxActive, active);
    try {
      if (gates[kind]) await gates[kind].promise;
      const code = failures.get(kind) || failure;
      failures.delete(kind);
      if (code) throw codeError(code);
    } finally { active--; }
  }
  const native = {
    async beginPairing() { calls.begin++; if (gates.begin) await gates.begin.promise; },
    async finishPairing() { calls.finish++; if (gates.finish) await gates.finish.promise; return identity.slice().buffer; },
    async connect(host, port, buffer) {
      calls.connect++; endpoints.push({ host, port });
      assert.ok(new Uint8Array(buffer).some(x => x !== 0));
      if (gates.connect) await gates.connect.promise;
      if (failure) throw codeError(failure);
      session++;
    },
    async press(code) { await input('press', { code }); },
    async touch(x, y, phase) { await input('touch', { x, y, phase }); },
    async holdSelect(held) { await input('hold', { held }); },
    cancel() { calls.cancel++; },
    async disconnect() { calls.disconnect++; }
  };
  class Store {
    async initialize() {}
    contains(id) { return stored.has(id); }
    async load(id) { return stored.get(id)?.slice().buffer; }
    async save(id, buffer, current) {
      calls.save++;
      if (gates.save) await gates.save.promise;
      if (!current()) return false;
      stored.set(id, new Uint8Array(buffer).slice()); return true;
    }
    async remove(id) { stored.delete(id); }
  }
  class Discovery { start() { calls.start++; } stop() { calls.stop++; } }
  const prefs = {
    getSync(k, d) { return data.has(k) ? data.get(k) : d; },
    putSync(k, v) { data.set(k, v); }, deleteSync(k) { data.delete(k); }, async flush() {}
  };
  const fakeSetTimeout = (fn, delay) => { timers.set(++timerId, { fn, delay, due: now + delay }); return timerId; };
  const Controller = new Function('preferences', 'companion', 'RemoteState', 'CredentialStore', 'DeviceDiscovery',
    'Device', 'manualEndpoint', 'TouchGesturePump', 'TouchSample', 'setTimeout', 'clearTimeout', 'Date', controllerJS)(
    { getPreferencesSync() { return prefs; } }, native, RemoteState, Store, Discovery,
    Device, manual, TouchGesturePump, TouchSample, fakeSetTimeout, id => timers.delete(id),
    { now() { throw new Error('Wall-clock access must not control held SELECT duration'); } });
  const controller = new Controller();
  async function fireTimer(id) {
    const timer = timers.get(id);
    assert.ok(timer, 'requested timer exists');
    timers.delete(id); now = Math.max(now, timer.due); timer.fn(); await tick();
  }
  return {
    controller, target, data, stored, timers, calls, gates, identity, endpoints, events,
    setFailure(code) { failure = code; }, failNext(kind, code) { failures.set(kind, code); },
    maxActive() { return maxActive; },
    async advance(ms) {
      now += ms;
      for (const [id, timer] of timers) if (timer.due <= now) await fireTimer(id);
    }, fireTimer,
    async start() { await controller.initialize({}); controller.enterForeground(); },
    async connected() { await this.start(); await controller.connectSelected(); assert.equal(controller.state.connected, true); },
    async fire() { const [id, timer] = timers.entries().next().value; assert.equal(timer.delay, 3000); await fireTimer(id); }
  };
}
