const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const O = require('../docs/orientation.js');
function equivalent(a,b) { assert.ok(Math.abs(a.reduce((sum,v,i)=>sum+v*b[i],0)) > .999999); }
const identity = [0,0,0,1], s = Math.SQRT1_2;
assert.deepEqual(O.parse('Q1,42,1000000,0,0,0'), {time:42,q:identity});
for (const bad of ['Q1,42,0,0,0,0','Q1,42,9000000,0,0,0','Q2,42,1000000,0,0,0','Q1,42,NaN,0,0,0']) assert.equal(O.parse(bad),null);
// All six physical resting sides must map measured upward acceleration to
// display +Y, including upside-down starts and heading-only recentering.
const sides = [
  [identity,[0,0,1],'Screen up'], [[1,0,0,0],[0,0,-1],'Screen down'],
  [[s,0,0,s],[0,1,0],'Button side down'], [[-s,0,0,s],[0,-1,0],'Button side up'],
  [[0,-s,0,s],[1,0,0],'USB side down'], [[0,s,0,s],[-1,0,0],'USB side up']
];
function near(a,b) { assert.ok(Math.hypot(...a.map((v,i)=>v-b[i])) < 1e-6, `${a} != ${b}`); }
for (const [q,g,label] of sides) {
  near(O.gravity(q),g); assert.equal(O.pose(g),label);
  const cadGravity = [-g[0],-g[1],g[2]];
  near(O.rotate(O.multiply(O.tilt(g),[1,0,0,0]),cadGravity),[0,1,0]);
  for (const angle of [0,O.heading(q),Math.PI/3]) {
    const total = O.multiply(O.absolute(q,angle),[1,0,0,0]);
    near(O.rotate(total,cadGravity),[0,1,0]);
    equivalent(O.absolute(q,angle),O.absolute(q.map(v=>-v),angle));
    if (label==='Screen up') assert.ok(O.rotate(total,[0,0,1])[1] > .999);
    if (label==='Screen down') assert.ok(O.rotate(total,[0,0,1])[1] < -.999);
  }
  const gate = O.stability(); let result;
  for(let t=0;t<=640;t+=16) result=gate({time:t,q,accel:g,gyro:[0,0,0]});
  assert.equal(result.ready,true);
  assert.equal(gate({time:656,q,accel:g,gyro:[0,0,10]}).ready,false);
  assert.equal(gate({time:672,q,accel:[0,0,0],gyro:[0,0,0]}).ready,false);
}
const record = O.parse('Q2,42,1000000,0,0,0,0,0,8192,0,0,0');
assert.deepEqual(record.accel,[0,0,1]);
assert.equal(O.parse('Q2,42,1000000,0,0,0'),null);
const disagreement=O.stability();
for(let t=0;t<=800;t+=16) assert.equal(disagreement({time:t,q:identity,accel:[0,0,-1],gyro:[0,0,0]}).ready,false);
// Full turns cross quaternion sign boundaries without changing the rotation.
for(let degrees=0;degrees<=360;degrees+=5) {
  const radians=degrees*Math.PI/180, q=[0,0,Math.sin(radians/2),Math.cos(radians/2)];
  equivalent(O.absolute(q),O.absolute(q.map(v=>-v)));
}
for (const path of process.argv.slice(2)) {
  const samples=fs.readFileSync(path,'utf8').split('\n').map(line=>O.parse(line.trim())).filter(Boolean);
  const gate=O.stability(); const states=samples.map(gate);
  const label=path.includes('buttons-down')?'Button side down':'Screen up';
  assert.ok(samples.length>150); assert.ok(states.some(state=>state.ready));
  assert.equal(states.at(-1).pose,label); assert.equal(states.at(-1).ready,true);
  const last=samples.at(-1), total=O.multiply(O.absolute(last.q,O.heading(last.q)),[1,0,0,0]);
  if(label==='Screen up') assert.ok(O.rotate(total,[0,0,1])[1]>.99);
  else assert.ok(O.rotate(total,[0,1,0])[1]<-.99);
  const rate=(samples.length-1)*1000/(samples.at(-1).time-samples[0].time);
  console.log(`Hardware proof: ${samples.length} records at ${rate.toFixed(2)} Hz, ${label}, stable initialization passed.`);
}
// Run the actual serial lifecycle against a browser/USB mock: Q1, recenter,
// Stop/release, firmware failure, unplug, and old-firmware fallback.
const app = fs.readFileSync(require.resolve('../docs/app.js'),'utf8');
async function scenario(kind) {
  const elements = new Map(), writes = []; let released=0,closed=0,reads=0;
  const element = () => ({disabled:false,hidden:false,textContent:'',value:'auto',addEventListener(){},getContext(){return {}},setAttribute(){}});
  const reader = { async read() {
    reads++;
    if(reads === 1) return {value:new TextEncoder().encode(kind==='legacy'?'Unknown command: orientation fus (type help)\n':kind==='error'?'ERROR: return EggBert to HOME before orientation.\n':Array.from({length:50},(_,i)=>'Q2,'+(100+i*16)+',1000000,0,0,0,0,0,8192,0,0,0\n').join(''))};
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
