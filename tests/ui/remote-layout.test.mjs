// Executes the actual platform-independent ArkTS layout helper, not a copied formula.
// Node >= 22.6 supplies stripTypeScriptTypes; this is not an ArkUI compiler/render test.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { stripTypeScriptTypes } from 'node:module';
const source = fs.readFileSync(new URL('../../entry/src/main/ets/model/RemoteLayout.ets', import.meta.url), 'utf8');
const { RemoteLayout } = await import(`data:text/javascript;base64,${Buffer.from(stripTypeScriptTypes(source)).toString('base64')}`);
let count = 0;
function check(name, test) { test(); count++; console.log(`PASS ${name}`); }
check('320dp phone keeps 16dp gutters, three 90dp actions and >=48dp direction targets', () => {
  const v = new RemoteLayout(320, 568, 1);
  assert.equal(v.contentWidth, 288); assert.equal(v.side, false); assert.equal(v.dock, true);
  assert.ok((v.padSize - 36) / 3 >= 48); assert.ok(v.actionWidth * 3 + 16 <= v.contentWidth);
});
check('393dp portrait uses 24dp gutters and 340dp maximum dpad', () => {
  const v = new RemoteLayout(393, 852, 1);
  assert.equal(v.contentWidth, 345); assert.equal(v.padSize, 340); assert.equal(v.dock, true);
});
check('800x360 landscape separates the 120dp side actions', () => {
  const v = new RemoteLayout(800, 360, 1);
  assert.equal(v.side, true); assert.equal(v.actionWidth, 120); assert.equal(v.contentWidth, 752);
  assert.ok(v.padSize + 136 <= v.contentWidth);
});
check('large font switches landscape to scrollable vertical controls', () => {
  const v = new RemoteLayout(800, 360, 2);
  assert.equal(v.side, false); assert.equal(v.dock, false);
});
check('short windows scroll instead of pinning the dock', () => assert.equal(new RemoteLayout(360, 320, 1).dock, false));
check('narrow split screen uses list fallback rather than <48dp grid cells', () => assert.ok(new RemoteLayout(180, 640, 1).padSize < 180));
check('tablet content is bounded to 520dp in portrait', () => assert.equal(new RemoteLayout(800, 1000, 1).contentWidth, 520));
check('large landscape content is bounded to 1040dp', () => assert.equal(new RemoteLayout(1600, 900, 1).contentWidth, 1040));
check('invalid initial zero area never gives negative geometry', () => {
 const v = new RemoteLayout(0, 0, 1); assert.equal(v.contentWidth, 0); assert.equal(v.padSize, 0);
});
console.log(`${count} layout policy tests passed.`);

// Execute the actual non-rendering page handlers using a stub controller.
// This checks state transitions; it does not emulate ArkUI reactivity or rendering.
const page = fs.readFileSync(new URL('../../entry/src/main/ets/pages/Index.ets', import.meta.url), 'utf8');
const prefix = page.slice(page.indexOf('struct Index {'), page.indexOf('  @Builder'))
  .replace('struct Index', 'class Index').replaceAll('@State ', '');
const pageClass = stripTypeScriptTypes(`${prefix}\n}\nglobalThis.TestIndex = Index;`);
const calls = [];
const state = { foreground: true, connected: true, busy: false, buttonInFlight: false, ready: true, needsPin: false, selected: undefined };
const controller = {
 state,
 disconnect() { calls.push('disconnect'); state.connected = false; state.needsPin = false; },
 select(device) { calls.push(`select:${device.id}`); state.selected = device; },
 connectSelected() { calls.push('connect'); state.busy = true; }
};
const factory = new Function('remoteController', 'RemoteLayout', pageClass + '\nreturn TestIndex;');
const Page = factory(controller, RemoteLayout);
const view = new Page();
check('transient input lock keeps appearance but suppresses clicks', () => {
 state.buttonInFlight = true; assert.equal(view.controlsEnabled(), false); assert.equal(view.controlsAvailable(), true);
 state.buttonInFlight = false; assert.equal(view.controlsEnabled(), true);
});
check('disconnection and background suppress input and enabled appearance', () => {
 state.connected = false; assert.equal(view.controlsEnabled(), false); assert.equal(view.controlsAvailable(), false);
 state.connected = true; state.foreground = false; assert.equal(view.controlsEnabled(), false); assert.equal(view.canSelect(), false);
 state.foreground = true;
});
check('Back dismisses only the options overlay and clears drafts', () => {
 view.panel = 'options'; view.manualAddress = '192.0.2.1'; view.manualPort = '12345';
 assert.equal(view.onBackPress(), true); assert.equal(view.panel, ''); assert.equal(view.manualAddress, ''); assert.equal(view.manualPort, '');
 assert.equal(calls.length, 0);
});
check('Back during PIN cancels the session even while verification is busy', () => {
 state.needsPin = true; state.busy = true; assert.equal(view.onBackPress(), true); assert.deepEqual(calls, ['disconnect']);
 state.busy = false; calls.length = 0;
});
check('Back without overlay permits normal page navigation', () => assert.equal(view.onBackPress(), false));
check('selecting a device connects once and ignores repeated busy selection', () => {
 view.panel = 'devices'; const device = {id:'tv', host:'192.0.2.1', port:12345};
 view.selectDevice(device); view.selectDevice(device);
 assert.deepEqual(calls, ['select:tv', 'connect']); assert.equal(view.panel, '');
 state.busy = false; calls.length = 0;
});
check('manual address starts with selected endpoint, not a hard-coded port', () => {
 view.openManual(); assert.equal(view.panel, 'manual'); assert.equal(view.manualAddress, '192.0.2.1'); assert.equal(view.manualPort, '12345');
});
check('overlay is hidden while backgrounded and background controls are isolated', () => {
 state.foreground = false; assert.equal(view.modalOpen(), false); state.foreground = true; assert.equal(view.modalOpen(), true);
 assert.ok(page.includes('.enabled(!this.modalOpen())'));
 assert.ok(page.includes(".accessibilityLevel(this.modalOpen() ? 'no-hide-descendants' : 'auto')"));
});
console.log(`${count} total layout and UI-handler checks passed (not ArkUI rendering).`);
