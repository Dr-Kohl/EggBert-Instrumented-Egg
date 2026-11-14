const assert=require('node:assert/strict');
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const root=path.join(__dirname,'..');
const elements=new Map(), bands=[];
const ctx=new Proxy({measureText:()=>({width:50}),fillRect:(...args)=>bands.push(args)},{get:(o,k)=>k in o?o[k]:()=>{}});
const element=()=>({hidden:false,disabled:false,textContent:'',value:'auto',width:1000,height:340,style:{},parentElement:{hidden:false},options:['auto','2','4','32','fit'].map(value=>({value})),classList:{toggle(){}},addEventListener(){},setAttribute(){},getContext:()=>ctx});
const context=vm.createContext({console,Uint8Array,Int16Array,Float32Array,Float64Array,DataView,TextEncoder,TextDecoder,Blob,setTimeout,clearTimeout,performance,window:{addEventListener(){}},navigator:{},document:{querySelector(id){if(!elements.has(id))elements.set(id,element());return elements.get(id);}}});
for(const file of ['hammer.js','app.js']) vm.runInContext(fs.readFileSync(path.join(root,'docs',file),'utf8'),context);
const run=code=>vm.runInContext(code,context);
// Raw samples straddle 20 g, include vector-only crossings and a later bounce.
context.high=[0,0,1919,0,0,1920,0,0,0,1400,1400,0,0,0,3000];
run(`capture={dual:true,validCrc:true,flags:1,sampleCount:5,sampleRate:3840,triggerOffset:1,rawHigh:Int16Array.from(high),auto:{magnitude:Float32Array.from([19.99,20.00064,0,20.625,31.251])}};`);
const result=run('EggHammer.analyze(capture)');
assert.equal(result.count,3); assert.equal(result.milliseconds,3*1000/3840); assert.equal(result.peakG,31.2);
assert.deepEqual(Array.from(result.intervals, x=>Array.from(x)),[[1,2],[3,5]]);
for(const flags of [0,5,33,65]) {run(`capture.flags=${flags}`); assert.equal(run('EggHammer.analyze(capture).milliseconds'),null);}
run('capture.flags=1; capture.rawHigh[0]=30413'); assert.equal(run('EggHammer.analyze(capture).milliseconds'),null);
run('capture.dual=false'); assert.equal(run('EggHammer.analyze(capture).milliseconds'),null);
// Exercise the complete view, switching, shade rendering and fixed full-record totals.
const demo=new Uint8Array(80+19200*12), header=new DataView(demo.buffer);
demo.set([69,71,71,50,2,17]); header.setUint16(6,80,true);
for(const [offset,value] of [[8,3840],[12,19200],[16,2304],[20,16895],[28,0],[32,10417],[64,0],[68,16384]]) header.setUint32(offset,value,true);
header.setUint16(24,2048,true); header.setUint16(26,12,true); header.setUint16(36,0x73,true); header.setUint16(38,320,true);
for(let axis=0;axis<3;axis++) header.setUint32(52+axis*4,65536,true);
for(let i=0;i<19200;i++) {
 let high=96;
 for(const [start,peak] of [[2304,3500],[5376,2800],[8448,2300]]) if(i>=start && i<start+154) high=peak;
 header.setInt16(80+i*12+4,Math.min(32767,Math.round(high*0.010417*2048)),true);
 header.setInt16(80+i*12+10,high,true);
}
context.bytes=demo;
header.setUint32(28,run('crc32(bytes.subarray(80))'),true);
header.setUint32(76,run('crc32(bytes.subarray(0,76))'),true);
run('displayCapture(bytes); selectSensor("low"); chartMode="axes"; verticalScale="32"; setAnalysisView("hammer")');
assert.equal(elements.get('#triggerType').textContent,'Impact Â· Low-g clipped');
assert.equal(run('sensorSource'),'high'); assert.equal(run('chartMode'),'magnitude');
assert.equal(elements.get('#hammerTime').textContent,'120.3 ms');
assert.equal(elements.get('#hammerPeak').textContent,'36.4 g');
assert.equal(elements.get('#axesButton').disabled,true); assert.equal(elements.get('#sensorSource').disabled,true);
assert.ok(bands.length>0);
run('setView(2,0.1)'); assert.equal(elements.get('#hammerTime').textContent,'120.3 ms');
run('setAnalysisView("normal")'); assert.equal(run('sensorSource'),'low'); assert.equal(run('chartMode'),'axes'); assert.equal(run('verticalScale'),'32');
assert.equal(elements.get('#hammerSummary').hidden,true); assert.equal(elements.get('#axesButton').disabled,false);
run('setAnalysisView("hammer"); displayCapture(bytes)'); assert.equal(run('analysisMode'),'normal');
console.log('Hammer analysis: thresholds, vector magnitude, bounces, invalid/clipped/V1 data, fixed totals, rendering and view restoration passed.');
