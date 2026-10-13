// Node >=24 logic-only checks. These do NOT compile ArkTS or validate HarmonyOS APIs.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import { stripTypeScriptTypes } from 'node:module';
const ets = (relative) => stripTypeScriptTypes(fs.readFileSync(new URL('../src/main/ets/' + relative, import.meta.url), 'utf8').replace(/^import .*;\n/gm, '').replace(/^@Observed\n/gm, '').replace(/export default class /g, 'class ').replace(/export /g, ''));
const Device = new Function(ets('model/Device.ets') + '; return Device;')();
const manual = new Function(ets('model/Device.ets') + '; return manualEndpoint;')();
const RemoteState = new Function(ets('model/RemoteState.ets') + '; return RemoteState;')();
const controllerJS = ets('model/RemoteController.ets') + '; return RemoteController;';
const discoveryJS = ets('discovery/DeviceDiscovery.ets') + '; return DeviceDiscovery;';
const deferred = () => { let resolve; let reject; const promise = new Promise((r,j)=>{resolve=r;reject=j;}); return {promise,resolve,reject}; };
const tick = () => new Promise(r=>setImmediate(r));
const codeError = code => Object.assign(new Error('DO NOT DISPLAY PEER DATA'), {code});
function fixture({saved=false,resume=false}={}) {
  const target = new Device('mdns:TV', 'TV', '192.168.1.2', 12345);
  const data = new Map(saved ? [['last_id',target.id],['last_name',target.name],['last_host',target.host],['last_port',target.port],['resume_connection',resume]] : []);
  const identity = new Uint8Array([1,2,3,4]);
  const stored = new Map(saved ? [[target.id,identity]] : []);
  const timers = new Map(); let timerId=0;
  const endpoints = [];
  const calls = {begin:0,finish:0,connect:0,press:0,cancel:0,disconnect:0,save:0,start:0,stop:0};
  const gates = {begin:undefined,finish:undefined,connect:undefined,press:undefined,save:undefined};
  let failure='';
  const native = {
    async beginPairing(){calls.begin++;if(gates.begin)await gates.begin.promise;},
    async finishPairing(){calls.finish++;if(gates.finish)await gates.finish.promise;return identity.slice().buffer;},
    async connect(h,p,buf){calls.connect++;endpoints.push({host:h,port:p});assert.ok(new Uint8Array(buf).some(x=>x!==0));if(gates.connect)await gates.connect.promise;if(failure)throw codeError(failure);},
    async press(){calls.press++;if(gates.press)await gates.press.promise;if(failure)throw codeError(failure);},
    cancel(){calls.cancel++;},async disconnect(){calls.disconnect++;}
  };
  class Store {
    async initialize(){} contains(id){return stored.has(id);}
    async load(id){return stored.get(id)?.slice().buffer;}
    async save(id,buf,current){calls.save++;if(gates.save)await gates.save.promise;if(!current())return false;stored.set(id,new Uint8Array(buf).slice());return true;}
    async remove(id){stored.delete(id);}
  }
  class Discovery {start(){calls.start++;}stop(){calls.stop++;}}
  const prefs={getSync(k,d){return data.has(k)?data.get(k):d;},putSync(k,v){data.set(k,v);},deleteSync(k){data.delete(k);},async flush(){}};
  const Controller=new Function('preferences','companion','RemoteState','CredentialStore','DeviceDiscovery','Device','manualEndpoint','setTimeout','clearTimeout',controllerJS)({getPreferencesSync(){return prefs;}},native,RemoteState,Store,Discovery,Device,manual,(fn,delay)=>{timers.set(++timerId,{fn,delay});return timerId;},id=>timers.delete(id));
  const controller=new Controller();
  return {controller,target,data,stored,timers,calls,gates,identity,endpoints,setFailure(x){failure=x;},async start(){await controller.initialize({});controller.enterForeground();},async fire(){const [id,t]=timers.entries().next().value;timers.delete(id);assert.equal(t.delay,3000);t.fn();await tick();}};
}
let count=0;
async function check(name, fn){await fn();count++;console.log('PASS '+name);}
await check('manual endpoint requires explicit port and numeric unicast IPv4',()=>{
  assert.equal(manual('192.168.001.2','12345').host,'192.168.1.2');
  for(const [a,p] of [['192.168.1.2',''],['localhost','123'],['127.0.0.1','123'],['224.0.0.1','123'],['1.2.3.256','123'],['1.2.3.4','65536'],['1.2.3.4','0'],['1.2.3.4','1x']])assert.equal(manual(a,p),undefined);
});
await check('manual IPv6 accepts compression and numeric scope, rejects unsafe/malformed inputs',()=>{
  assert.equal(manual('2001:db8::1','42').host,'2001:db8:0:0:0:0:0:1');
  assert.equal(manual('[fe80::abcd%12]','42').host,'fe80:0:0:0:0:0:0:abcd%12');
  for(const host of ['::','::1','ff02::1','fe80::1%eth0','fe80::1%0','a:b:c','2001:::1','1:2:3:4:5:6:7:8::','::ffff:192.0.2.1'])assert.equal(manual(host,'42'),undefined);
});
await check('duplicate connect is guarded; unpaired native begin does not mean connected',async()=>{const f=fixture();await f.start();f.controller.select(f.target);f.gates.begin=deferred();const p=f.controller.connectSelected();await tick();await f.controller.connectSelected();assert.equal(f.calls.begin,1);assert.equal(f.controller.state.connected,false);f.gates.begin.resolve();await p;assert.equal(f.controller.state.needsPin,true);});
await check('late begin completion after background cannot show PIN',async()=>{const f=fixture();await f.start();f.controller.select(f.target);f.gates.begin=deferred();const p=f.controller.connectSelected();await tick();f.controller.leaveForeground();f.gates.begin.resolve();await p;assert.equal(f.controller.state.needsPin,false);assert.equal(f.controller.state.connected,false);assert.ok(f.calls.cancel>0);assert.equal(f.calls.stop,1);});
await check('connected only after native verified session startup',async()=>{const f=fixture();await f.start();f.controller.select(f.target);await f.controller.connectSelected();f.controller.state.pin='1234';f.gates.connect=deferred();const p=f.controller.finishPairing();await tick();assert.equal(f.calls.save,1);assert.equal(f.controller.state.pin,'');assert.equal(f.controller.state.connected,false);f.gates.connect.resolve();await p;assert.equal(f.controller.state.connected,true);});
await check('pairing cancellation never saves late identity',async()=>{const f=fixture();await f.start();f.controller.select(f.target);await f.controller.connectSelected();f.controller.state.pin='1234';f.gates.finish=deferred();const p=f.controller.finishPairing();await tick();f.controller.disconnect();f.gates.finish.resolve();await p;assert.equal(f.calls.save,0);assert.equal(f.calls.connect,0);assert.equal(f.controller.state.connected,false);});
await check('save cancelled in background cannot connect',async()=>{const f=fixture();await f.start();f.controller.select(f.target);await f.controller.connectSelected();f.controller.state.pin='1234';f.gates.save=deferred();const p=f.controller.finishPairing();await tick();f.controller.leaveForeground();f.gates.save.resolve();await p;assert.equal(f.stored.size,0);assert.equal(f.calls.connect,0);});
await check('PIN must be exactly four digits',async()=>{const f=fixture();await f.start();f.controller.select(f.target);await f.controller.connectSelected();for(const pin of ['','123','12345','a123']){f.controller.state.pin=pin;await f.controller.finishPairing();}assert.equal(f.calls.finish,0);});
await check('two reconnect attempts per foreground, each delayed 3000ms',async()=>{const f=fixture({saved:true,resume:true});f.setFailure('TIMEOUT');await f.start();assert.equal(f.timers.size,1);await f.fire();assert.equal(f.calls.connect,1);assert.equal(f.timers.size,1);await f.fire();assert.equal(f.calls.connect,2);assert.equal(f.timers.size,0);f.controller.leaveForeground();f.controller.enterForeground();assert.equal(f.timers.size,1);});
await check('reconnect refreshes same service endpoint but keeps credential identity',async()=>{const f=fixture({saved:true,resume:true});await f.start();f.controller.state.devices=[new Device(f.target.id,'TV','192.168.1.99',23456),new Device('mdns:Other','Other','192.168.1.20',34567)];await f.fire();assert.deepEqual(f.endpoints,[{host:'192.168.1.99',port:23456}]);assert.equal(f.calls.begin,0);assert.equal(f.controller.state.connected,true);});
await check('identity/auth/protocol failures do not retry or pair',async()=>{for(const code of ['IDENTITY_CHANGED','AUTH_FAILED','PROTOCOL_ERROR','INTERNAL_ERROR']){const f=fixture({saved:true,resume:true});f.setFailure(code);await f.start();await f.fire();assert.equal(f.calls.connect,1);assert.equal(f.timers.size,0);assert.equal(f.calls.begin,0);assert.equal(f.controller.state.connected,false);assert.equal(f.controller.state.status.includes('DO NOT DISPLAY'),false);}});
await check('explicit disconnect suppresses reconnect across foreground cycles',async()=>{const f=fixture({saved:true,resume:true});await f.start();f.controller.disconnect();assert.equal(f.timers.size,0);f.controller.leaveForeground();f.controller.enterForeground();assert.equal(f.timers.size,0);assert.equal(f.data.get('resume_connection'),false);});
await check('background cancels pending reconnect',async()=>{const f=fixture({saved:true,resume:true});await f.start();f.controller.leaveForeground();assert.equal(f.timers.size,0);assert.equal(f.calls.connect,0);});
await check('duplicate buttons suppressed and failed button never replayed',async()=>{const f=fixture({saved:true});await f.start();await f.controller.connectSelected();f.gates.press=deferred();f.setFailure('DISCONNECTED');const p=f.controller.press(1);await tick();await f.controller.press(1);assert.equal(f.calls.press,1);f.gates.press.resolve();await p;assert.equal(f.controller.state.connected,false);assert.equal(f.timers.size,0);});
await check('forget confirmation is bound to selected device',async()=>{const f=fixture({saved:true});await f.start();await f.controller.forgetSelected('wrong-id');assert.equal(f.stored.size,1);await f.controller.forgetSelected(f.target.id);assert.equal(f.stored.size,0);});
function discoveryFixture(){const updates=[];const services=[];const resolves=[];const timers=new Map();let id=0;const mdns={createDiscoveryService(){const callbacks=new Map();const service={callbacks,start:0,stop:0,on(k,f){callbacks.set(k,f);},off(k){callbacks.delete(k);},startSearchingMDNS(){this.start++;},stopSearchingMDNS(){this.stop++;}};services.push(service);return service;},resolveLocalService(context,info){const d=deferred();resolves.push({info,...d});return d.promise;}};const Discovery=new Function('mdns','Device','setTimeout','clearTimeout',discoveryJS)(mdns,Device,(fn,delay)=>{timers.set(++id,{fn,delay});return id;},id=>timers.delete(id));const discovery=new Discovery({},(devices,message)=>updates.push({devices,message}));return {discovery,services,resolves,updates,timers};}
const info=(name)=>({serviceName:name,serviceType:'_companion-link._tcp'});
const resolved=(name,port)=>({...info(name),host:{address:'192.168.1.22'},port});
await check('mDNS discoveryStart success code zero does not stop discovery',()=>{const f=discoveryFixture();f.discovery.start();f.services[0].callbacks.get('discoveryStart')({serviceInfo:info('TV'),errorCode:0});assert.equal(f.services[0].stop,0);assert.equal(f.services[0].callbacks.size,3);});
await check('mDNS discoveryStart failure stops cleanly',()=>{const f=discoveryFixture();f.discovery.start();f.services[0].callbacks.get('discoveryStart')({serviceInfo:info('TV'),errorCode:2});assert.equal(f.services[0].stop,1);assert.equal(f.timers.size,0);});
await check('mDNS resolves actual dynamic port and deduplicates found events',async()=>{const f=discoveryFixture();f.discovery.start();f.discovery.start();assert.equal(f.services.length,1);const cb=f.services[0].callbacks.get('serviceFound');cb(info('TV'));cb(info('TV'));assert.equal(f.resolves.length,1);f.resolves[0].resolve(resolved('TV',54321));await tick();assert.equal(f.updates.at(-1).devices[0].port,54321);});
await check('stale mDNS resolves cannot populate or drain new generation',async()=>{const f=discoveryFixture();f.discovery.start();f.services[0].callbacks.get('serviceFound')(info('old'));f.discovery.stop();f.discovery.start();f.services[1].callbacks.get('serviceFound')(info('new'));f.services[1].callbacks.get('serviceFound')(info('queued'));assert.equal(f.resolves.length,2);f.resolves[0].resolve(resolved('old',1));await tick();assert.equal(f.resolves.length,2);assert.equal(f.updates.at(-1).devices.length,0);f.resolves[1].resolve(resolved('new',2000));await tick();assert.equal(f.resolves.length,3);assert.deepEqual(f.updates.at(-1).devices.map(x=>x.name),['new']);});
await check('lost service resolve ignored and stop removes listeners/timer',async()=>{const f=discoveryFixture();f.discovery.start();f.services[0].callbacks.get('serviceFound')(info('TV'));f.services[0].callbacks.get('serviceLost')(info('TV'));f.resolves[0].resolve(resolved('TV',99));await tick();assert.equal(f.updates.at(-1).devices.length,0);f.discovery.stop();assert.equal(f.services[0].stop,1);assert.equal(f.services[0].callbacks.size,0);assert.equal(f.timers.size,0);});
await check('invalid resolved address/port never becomes selectable',async()=>{const f=discoveryFixture();f.discovery.start();f.services[0].callbacks.get('serviceFound')(info('TV'));f.resolves[0].resolve(resolved('TV',0));await tick();assert.equal(f.updates.at(-1).devices.length,0);});
console.log(`${count} mock logic checks passed. NOT HarmonyOS SDK/ArkTS compilation or device validation.`);
