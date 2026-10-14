// Execute actual ArkTS pointer policy and component handlers. This is not ArkUI rendering.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { stripTypeScriptTypes } from 'node:module';
const load = (path) => fs.readFileSync(new URL(path, import.meta.url), 'utf8');
const source = load('../../entry/src/main/ets/model/TouchContact.ets');
const { TouchContact } = await import(`data:text/javascript;base64,${Buffer.from(stripTypeScriptTypes(source)).toString('base64')}`);
let count = 0;
function check(name, test) { test(); count++; console.log(`PASS ${name}`); }
const contact = () => new TouchContact();
check('negotiated coordinates use each actual surface dimension and clamp', () => {
 const c = contact(); assert.ok(c.begin(1, 50, 100, 200, 400, 0));
 assert.equal(c.x, 250); assert.equal(c.y, 250);
 c.update(-5, 450); assert.equal(c.x, 0); assert.equal(c.y, 1000);
});
check('invalid geometry and non-finite coordinates are rejected', () => {
 for (const size of [0, -1, NaN, Infinity]) assert.equal(contact().begin(1, 0, 0, size, 100, 0), false);
 assert.equal(contact().begin(1, NaN, 0, 100, 100, 0), false);
});
check('second begin cannot retarget an existing gesture', () => {
 const c = contact(); c.begin(1, 0, 0, 100, 100, 0);
 assert.equal(c.begin(2, 80, 80, 100, 100, 0), false); assert.equal(c.pointerId, 1);
});
check('short tap selects and 500ms boundary does not', () => {
 const c = contact(); c.begin(1, 0, 0, 100, 100, 1e9); assert.equal(c.finish(1e9 + 499e6), true);
 c.begin(1, 0, 0, 100, 100, 0); assert.equal(c.finish(500e6), false);
});
check('out-and-back movement never turns a swipe into a tap or hold', () => {
 const c = contact(); c.begin(1, 0, 0, 100, 100, 0); c.update(8, 0); c.update(0, 0);
 assert.equal(c.hold(), false); assert.equal(c.finish(10e6), false);
});
check('held contact is sent once and does not tap', () => {
 const c = contact(); c.begin(1, 0, 0, 100, 100, 0);
 assert.equal(c.hold(), true); assert.equal(c.hold(), false); assert.equal(c.finish(600e6), false);
});
check('movement is throttled to 16ms on monotonic nanoseconds', () => {
 const c = contact(); c.begin(1, 0, 0, 100, 100, 1e9);
 assert.equal(c.shouldMove(1e9 + 15e6), false); assert.equal(c.shouldMove(1e9 + 16e6), true);
 assert.equal(c.shouldMove(1e9 + 20e6), false); assert.equal(c.shouldMove(1e9 + 32e6), true);
});
check('invalid or reversed timestamps do not generate taps', () => {
 for (const time of [NaN, Infinity, -1]) { const c = contact(); c.begin(1, 0, 0, 100, 100, 0); assert.equal(c.finish(time), false); }
});

const component = load('../../entry/src/main/ets/components/RemoteTouchPad.ets');
const prefix = component.slice(component.indexOf('export struct RemoteTouchPad'), component.indexOf('  build()'))
 .replace('export struct RemoteTouchPad', 'class RemoteTouchPad')
 .replace(/@Watch\('[^']+'\) /g, '').replaceAll('@Prop ', '').replaceAll('@State ', '');
const js = stripTypeScriptTypes(`${prefix}\n}`) + "\nreturn RemoteTouchPad;";
const timers = new Map(); let nextTimer = 0;
const TouchType = { Down: 0, Up: 1, Move: 2, Cancel: 3 };
const Component = new Function('TouchContact', 'TouchType', 'setTimeout', 'clearTimeout', js)(
 TouchContact, TouchType, (fn, ms) => { timers.set(++nextTimer, { fn, ms }); return nextTimer; }, id => timers.delete(id));
function setup() {
 timers.clear(); const pad = new Component(); const calls = [];
 pad.inputEnabled = true; pad.areaWidth = 200; pad.areaHeight = 200;
 pad.onBegin = (x,y) => { calls.push(['begin',x,y]); return true; };
 pad.onMove = (x,y) => calls.push(['move',x,y]);
 pad.onEnd = (x,y,tap) => calls.push(['end',x,y,tap]);
 pad.onHold = () => calls.push(['hold']); pad.onCancel = () => calls.push(['cancel']);
 return {pad,calls};
}
function event(type, x=100, y=100, time=0, id=1, touches) {
 const point = { id,x,y };
 return { type, timestamp: time*1e6, changedTouches: [point], touches: touches ?? [point], stopPropagation() {}, getHistoricalPoints() { return []; } };
}
function fire(ms) { const entry = [...timers.entries()].find(([, t]) => t.ms === ms); assert.ok(entry); timers.delete(entry[0]); entry[1].fn(); }
check('actual handler emits begin, final END and one tap with no timers left', () => {
 const {pad,calls}=setup(); pad.handleTouch(event(0)); pad.handleTouch(event(1,100,100,80));
 assert.deepEqual(calls,[['begin',500,500],['end',500,500,true]]); assert.equal(timers.size,0);
});
check('actual swipe retains latest final coordinates even below move throttle', () => {
 const {pad,calls}=setup(); pad.handleTouch(event(0)); pad.handleTouch(event(2,120,110,16)); pad.handleTouch(event(1,180,160,20));
 assert.deepEqual(calls,[['begin',500,500],['move',600,550],['end',900,800,false]]);
});
check('long press sends hold once and releases without click', () => {
 const {pad,calls}=setup(); pad.handleTouch(event(0)); fire(500); pad.handleTouch(event(1,100,100,800));
 assert.deepEqual(calls,[['begin',500,500],['hold'],['end',500,500,false]]); assert.equal(timers.size,0);
});
check('moving away then returning prevents delayed long press', () => {
 const {pad,calls}=setup(); pad.handleTouch(event(0)); pad.handleTouch(event(2,110,100,20)); pad.handleTouch(event(2,100,100,40)); fire(500);
 assert.equal(calls.some(c=>c[0]==='hold'),false); pad.handleTouch(event(1,100,100,600)); assert.equal(calls.at(-1)[3],false);
});
check('second finger cancels and its release cannot select or restart', () => {
 const {pad,calls}=setup(); pad.handleTouch(event(0)); pad.handleTouch(event(0,120,100,30,2,[{id:1,x:100,y:100},{id:2,x:120,y:100}]));
 pad.handleTouch(event(1,120,100,60,2)); pad.handleTouch(event(1,100,100,80,1));
 assert.deepEqual(calls,[['begin',500,500],['cancel']]); assert.equal(timers.size,0);
});
check('cancel, disappearing and disable release once without tap', () => {
 for (const reason of ['cancel','disappear','disable']) {
  const {pad,calls}=setup(); pad.handleTouch(event(0));
  if(reason==='cancel')pad.handleTouch(event(3));
  if(reason==='disappear')pad.aboutToDisappear();
  if(reason==='disable'){pad.inputEnabled=false;pad.availabilityChanged();}
  pad.aboutToDisappear(); assert.deepEqual(calls,[['begin',500,500],['cancel']]); assert.equal(timers.size,0);
 }
});
check('ten-second watchdog cancels even a held contact', () => {
 const {pad,calls}=setup(); pad.handleTouch(event(0)); fire(500); fire(10000);
 assert.deepEqual(calls,[['begin',500,500],['hold'],['cancel']]); assert.equal(timers.size,0);
});
check('rejected begin allocates no timers and disabled pad emits nothing', () => {
 const {pad,calls}=setup(); pad.onBegin=()=>false; pad.handleTouch(event(0)); assert.equal(timers.size,0);
 pad.inputEnabled=false;pad.handleTouch(event(0)); assert.deepEqual(calls,[]);
});
check('missing pointer or non-finite move fails closed', () => {
 for(const e of [{type:2,timestamp:20e6,changedTouches:[],touches:[],stopPropagation(){},getHistoricalPoints(){return [];}},event(2,NaN,0,20)]) {
  const {pad,calls}=setup();pad.handleTouch(event(0));pad.handleTouch(e);
  assert.deepEqual(calls,[['begin',500,500],['cancel']]);
 }
});

check('coalesced out-and-back motion never becomes a tap or long press', () => {
 const {pad,calls}=setup();pad.handleTouch(event(0));
 const coalesced=event(2,100,100,20);
 coalesced.getHistoricalPoints=()=>[{touchObject:{id:1,x:115,y:100},timestamp:10e6}];
 pad.handleTouch(coalesced);fire(500);pad.handleTouch(event(1,100,100,600));
 assert.equal(calls.some(c=>c[0]==='hold'),false);assert.equal(calls.at(-1)[3],false);
});
check('coalesced second finger cancels rather than silently retargeting', () => {
 const {pad,calls}=setup();pad.handleTouch(event(0));
 const coalesced=event(2,100,100,20);coalesced.getHistoricalPoints=()=>[{touchObject:{id:2,x:100,y:100}}];
 pad.handleTouch(coalesced);assert.deepEqual(calls,[['begin',500,500],['cancel']]);
});


check('late cleared gesture timers cannot hold or cancel a new contact', () => {
 const {pad,calls}=setup();pad.handleTouch(event(0));
 const stale=[...timers.values()].map(t=>t.fn);pad.handleTouch(event(3));pad.handleTouch(event(0,50,50,100));
 const before=calls.length;for(const fn of stale)fn();
 assert.equal(calls.length,before);assert.equal(pad.contact.active,true);assert.equal(timers.size,2);
 pad.handleTouch(event(1,50,50,150));assert.deepEqual(calls.at(-1),['end',250,250,true]);
});
console.log(`${count} pointer policy and actual touch-handler checks passed (not ArkUI rendering).`);
