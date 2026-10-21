const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const app=fs.readFileSync(path.join(__dirname,'../docs/app.js'),'utf8');
const fixture=new Uint8Array(fs.readFileSync(path.join(__dirname,'../build/egg2-format-fixture.egg')));

function viewer() {
  const elements=new Map();
  const ctx=new Proxy({measureText:()=>({width:50})},{get:(o,k)=>k in o ? o[k] : ()=>{}});
  const element=()=>({disabled:false,hidden:false,textContent:'',value:'auto',width:1000,height:340,style:{},
    options:['auto','2','4','8','16','32','64','128','256','320','fit'].map(value=>({value})),
    classList:{toggle(){}},addEventListener(){},setAttribute(){},getContext:()=>ctx});
  const context=vm.createContext({console,Uint8Array,Int16Array,Float32Array,DataView,TextEncoder,TextDecoder,
    Blob,setTimeout,clearTimeout,performance,window:{addEventListener(){}},navigator:{},
    document:{querySelector(selector){if(!elements.has(selector))elements.set(selector,element());return elements.get(selector);}}});
  vm.runInContext(app,context);
  return {context,elements,run:code=>vm.runInContext(code,context)};
}
function headerCrc(v,bytes) { const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength); v.context.bytes=bytes; view.setUint32(76,v.run('crc32(bytes.subarray(0,76))'),true); }
function near(a,b) { assert.ok(Math.abs(a-b)<1e-5,`${a} != ${b}`); }

const v=viewer(); v.context.bytes=fixture;
const parsed=v.run('parseEgg(bytes)');
assert.equal(parsed.dual,true); assert.equal(parsed.validCrc,true); assert.equal(parsed.calibration.generation,7);
assert.equal(parsed.sampleCount,4); assert.equal(parsed.triggerOffset,1);
near(parsed.low.x[0],3048/2048); near(parsed.low.y[0],-2032/2048);
near(parsed.high.x[0],1.000032); near(parsed.high.y[1],-208.34);
assert.deepEqual(Array.from(parsed.rawLow.slice(0,3)),[2048,-2048,0]);
assert.equal(parsed.low.clipped[1],1); assert.equal(parsed.high.clipped[1],0); assert.equal(parsed.high.clipped[3],1);
near(parsed.low.z[2],0); // negative half-count rounding matches firmware
v.run('displayCapture(bytes); viewStart=0.0001; viewSeconds=0.0005; cursors=[0,2]; selectSensor("high")');
assert.equal(v.elements.get('#sensorControls').hidden,false);
assert.equal(v.elements.get('#sensorSource').value,'high');
assert.match(v.elements.get('#chartTitle').textContent,/High-g/);
near(v.run('viewStart'),0.0001); near(v.run('viewSeconds'),0.0005);
assert.deepEqual(Array.from(v.run('cursors')),[0,2]);
near(v.run('capture.x[0]'),1.000032);
const csv=v.run('captureCsvText()').trim().split(/\r?\n/).map(line=>line.split(','));
assert.equal(csv.length,5); assert.equal(csv[0].length,18);
assert.equal(csv[1][1],'1.488281'); assert.equal(csv[1][5],'1.000032');
assert.equal(csv[1][9],'2048'); assert.equal(csv[1][12],'96'); assert.equal(csv[1][17],'1');
assert.equal(Buffer.compare(Buffer.from(v.run('captureBytes')),Buffer.from(fixture)),0);
v.run('selectSensor("low")'); near(v.run('capture.x[0]'),3048/2048);

for (const name of fs.readdirSync(path.join(__dirname,'../docs/sample-data')).filter(n=>n.endsWith('.egg'))) {
  v.context.bytes=new Uint8Array(fs.readFileSync(path.join(__dirname,'../docs/sample-data',name)));
  v.run('displayCapture(bytes)');
  const dual=v.run('capture.dual'); assert.equal(v.run('capture.validCrc'),true);
  assert.equal(v.elements.get('#sensorControls').hidden,!dual);
  if (dual) {
    assert.equal(v.run('capture.sampleCount'),19200);
    assert.equal(v.run('captureCsvText().split("\\r\\n")[0].split(",").length'),18);
  } else assert.equal(v.run('captureCsvText().split("\\r\\n")[0]'),'Time (s),X (g),Y (g),Z (g),Magnitude (g)');
}
const corrupt=fixture.slice(); corrupt[corrupt.length-1]^=1; v.context.bytes=corrupt;
v.run('displayCapture(bytes)'); assert.equal(v.run('capture.validCrc'),false);
assert.equal(v.elements.get('#captureWarning').hidden,false);
assert.match(v.elements.get('#integrity').textContent,/FAILED/);
const loss=fixture.slice(); loss[5]|=0x20; headerCrc(v,loss); v.context.bytes=loss;
v.run('displayCapture(bytes)'); assert.equal(v.elements.get('#captureWarning').hidden,false);
assert.match(v.elements.get('#integrity').textContent,/Data loss/);
assert.equal(v.run('detectFlight()'),undefined);
assert.equal(v.run('captureCsvText().trim().split("\\r\\n")[1].split(",").at(-1)'),'0');
for (const offset of [32,40,52,64,68,76]) {
  const bad=fixture.slice(); bad[offset]^=1; v.context.bytes=bad;
  assert.throws(()=>v.run('parseEgg(bytes)'),/header CRC/);
}
const invalidScale=fixture.slice(); new DataView(invalidScale.buffer).setUint32(52,0,true); headerCrc(v,invalidScale);
v.context.bytes=invalidScale; assert.throws(()=>v.run('parseEgg(bytes)'),/metadata/);
v.context.bytes=fixture.subarray(0,fixture.length-1); assert.throws(()=>v.run('parseEgg(bytes)'),/Incomplete/);
const huge=fixture.slice(); new DataView(huge.buffer).setUint32(12,0xffffffff,true); v.context.bytes=huge;
assert.throws(()=>v.run('captureLayout(bytes)'),/metadata/);

async function serialCase(bytes,kind) {
  const w=viewer(); let index=0,closed=0,released=0; const writes=[];
  const preamble=Buffer.from('Accel raw: X=0\nDOWNLOAD - BINARY FOLLOWS\n');
  const stream=Buffer.concat([preamble,Buffer.from(bytes)]);
  // Cuts include the first three bytes of magic and a partial base/extended header.
  const cuts=[preamble.length+3,preamble.length+5,preamble.length+31,preamble.length+60,stream.length];
  const chunks=cuts.map((end,i)=>new Uint8Array(stream.subarray(i ? cuts[i-1] : 0,end)));
  if(kind==='truncated') chunks.pop();
  const reader={async read(){return index<chunks.length ? {value:chunks[index++]} : {done:true}},async cancel(){},releaseLock(){released++;}};
  w.context.fakePort={readable:{getReader:()=>reader},writable:{getWriter:()=>({async write(b){writes.push(new TextDecoder().decode(b))},releaseLock(){}})},async close(){closed++}};
  w.run('port=fakePort');
  if(kind==='ok') { await w.run('downloadCapture()'); assert.equal(closed,1); assert.equal(w.run('capture.validCrc'),true); }
  else { await assert.rejects(w.run('downloadCapture()'),kind==='crc' ? /CRC/ : /disconnected/); assert.equal(closed,0); }
  assert.equal(released,1); assert.deepEqual(writes,['download\n']);
}
(async()=>{
  await serialCase(fixture,'ok'); await serialCase(corrupt,'crc'); await serialCase(fixture,'truncated');
  console.log('Dual viewer: ARM fixture, calibration, raw data, switching, CSV, CRC, legacy files, and fragmented USB tests passed.');
})().catch(e=>{console.error(e);process.exitCode=1});
