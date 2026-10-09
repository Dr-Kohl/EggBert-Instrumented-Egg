const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const O = require('../docs/orientation.js');
function equivalent(a,b) { assert.ok(Math.abs(a.reduce((sum,v,i)=>sum+v*b[i],0)) > .999999); }
const identity = [0,0,0,1], s = Math.SQRT1_2;
assert.deepEqual(O.parse('Q1,42,1000000,0,0,0'), {time:42,q:identity});
for (const bad of ['Q1,42,0,0,0,0','Q1,42,9000000,0,0,0','Q2,42,1000000,0,0,0','Q1,42,NaN,0,0,0']) assert.equal(O.parse(bad),null);
// Home pose, turns around every physical axis, inverted pose, full turns,
// and both equivalent quaternion signs must preserve a proper rotation.
for (const ref of [identity,[s,0,0,s],[.5,.5,.5,.5]]) {
  equivalent(O.relative(ref,ref),identity);
  for (const [delta,expected] of [[[s,0,0,s],[s,0,0,s]],[[0,s,0,s],[0,-s,0,s]],[[0,0,s,s],[0,0,-s,s]],[[1,0,0,0],[1,0,0,0]],[identity,identity]]) {
    const current = O.multiply(ref,delta);
    equivalent(O.relative(ref,current),expected);
    equivalent(O.relative(ref,current.map(v=>-v)),expected);
  }
}
// Run the actual serial lifecycle against a browser/USB mock: Q1, recenter,
// Stop/release, firmware failure, unplug, and old-firmware fallback.
const app = fs.readFileSync(require.resolve('../docs/app.js'),'utf8');
async function scenario(kind) {
  const elements = new Map(), writes = []; let released=0,closed=0,reads=0;
  const element = () => ({disabled:false,hidden:false,textContent:'',value:'auto',addEventListener(){},getContext(){return {}},setAttribute(){}});
  const reader = { async read() {
    reads++;
    if(reads === 1) return {value:new TextEncoder().encode(kind==='legacy'?'Unknown command: orientation fus (type help)\n':kind==='error'?'ERROR: return EggBert to HOME before orientation.\n':'Q1,100,1000000,0,0,0\nQ1,800,1000000,0,0,0\n')};
    if(kind==='unplug') return {done:true};
    await new Promise(resolve=>{reader.resume=resolve;}); return {done:true};
  }, async cancel(){ reader.resume?.(); }, releaseLock(){released++;} };
  const fakePort = {readable:{getReader:()=>reader}, writable:{getWriter:()=>({write:async bytes=>writes.push(new TextDecoder().decode(bytes)),releaseLock(){}})},close:async()=>{closed++;}};
  const context = vm.createContext({console,TextEncoder,TextDecoder,setTimeout,clearTimeout,performance,Uint8Array,DataView,Math,Number,window:{addEventListener(){}},navigator:{},document:{querySelector(selector){if(!elements.has(selector))elements.set(selector,element());return elements.get(selector)}},EggOrientation:O});
  vm.runInContext(app,context);
  // Existing binary captures still decode with their recorded CRC and sample rate.
  for (const name of fs.readdirSync(require.resolve('../docs/orientation.js').replace('orientation.js','sample-data'))) {
    if (!name.endsWith('.egg')) continue;
    context.savedCapture = new Uint8Array(fs.readFileSync(require.resolve('../docs/orientation.js').replace('orientation.js','sample-data/'+name)));
    const capture = vm.runInContext('parseEgg(savedCapture)',context);
    assert.equal(capture.validCrc,true); assert.equal(capture.sampleCount,19200);
  }
  context.fakePort=fakePort;
  vm.runInContext('port=fakePort; orientationView={motionFrame:{quaternion:{copy(){} }}, target:{set(){},identity(){}}};',context);
  const run=vm.runInContext('setOrientation(true)',context); run.catch(()=>{});
  await new Promise(resolve=>setTimeout(resolve,10));
  if(kind==='error') await assert.rejects(run,/HOME/);
  else if(kind==='unplug') await assert.rejects(run,/disconnected/);
  else { if(kind==='fused') vm.runInContext('recenterOrientation()',context); await vm.runInContext('setOrientation(false)',context); await run; }
  assert.equal(released,1); assert.equal(closed,1);
  assert.equal(elements.get('#orientationButton').disabled,true);
  assert.ok(writes.includes('orientation fused\n'));
  if(kind==='legacy') assert.ok(writes.includes('orientation on\n'));
}
(async()=>{for(const kind of ['fused','legacy','error','unplug'])await scenario(kind);console.log('Orientation math and USB lifecycle checks passed.');})().catch(error=>{console.error(error);process.exitCode=1;});
