// Run with node tools/test_capture_viewer.cjs. No browser or packages required.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const elements = new Map();
const ctx = new Proxy({measureText: text => ({width: text.length * 8})}, {get: (obj, key) => obj[key] || (() => {})});
function element(id) {
  if (!elements.has(id)) elements.set(id, {
    value: '', classList: {toggle() {}}, style: {}, width: 1000, height: 340,
    options: ['auto','2','4','8','16','fit'].map(value => ({value})),
    listeners: {}, addEventListener(type, fn) {this.listeners[type] = fn;},
    setAttribute() {}, getContext: () => ctx,
    getBoundingClientRect: () => ({left: 0, top: 0, width: 1000, height: 340}),
    setPointerCapture() {}, hasPointerCapture: () => false,
  });
  return elements.get(id);
}
const sandbox = vm.createContext({console, Uint8Array, Float32Array, DataView, TextEncoder, TextDecoder,
  document: {querySelector: element}, window: {addEventListener() {}}, navigator: {}});
vm.runInContext(fs.readFileSync(path.join(__dirname, '../docs/app.js'), 'utf8'), sandbox);
const run = code => vm.runInContext(code, sandbox);
const json = code => JSON.parse(run(`JSON.stringify(${code})`));
const fire = (id, type, event = {}) => element(id).listeners[type]({target: element(id), ...event});
let files = 0;
for (const file of fs.readdirSync(path.join(__dirname, '../docs/sample-data')).filter(name => name.endsWith('.egg'))) {
  sandbox.bytes = new Uint8Array(fs.readFileSync(path.join(__dirname, '../docs/sample-data', file)));
  run('displayCapture(bytes)');
  assert.equal(run('capture.validCrc'), true, file);
  for (const axis of ['x', 'y', 'z', 'magnitude']) {
    assert.equal(run(`(() => {const v = capture.${axis}; const ids = peakPreservingIndices(v,0,v.length-1,924); return Math.max(...ids.map(i=>v[i])) === Math.max(...v) && Math.min(...ids.map(i=>v[i])) === Math.min(...v);})()`), true, file + axis);
  }
  files++;
}
sandbox.spikes = new Float32Array(19200);
sandbox.spikes[1234] = 15; sandbox.spikes[1235] = -12;
assert.ok(json('peakPreservingIndices(spikes,0,19199,924)').includes(1234));
assert.ok(json('peakPreservingIndices(spikes,0,19199,924)').includes(1235));
assert.deepEqual(json('peakPreservingIndices(spikes,1200,1300,924)'), Array.from({length:101}, (_,i)=>1200+i));
const auto = json('currentDomain()');
run('setView(1,0.5)'); assert.deepEqual(json('currentDomain()'), auto);
run('fitVisibleData()'); const fit = json('currentDomain()');
run('setView(2,0.5)'); assert.deepEqual(json('currentDomain()'), fit);
run('verticalScale="2"'); assert.deepEqual(json('currentDomain()'), [-2,2]);
run('setChartMode("magnitude")'); assert.deepEqual(json('currentDomain()'), [0,2]);
run('setCursor(0,0.8021); setCursor(1,1.5388)');
assert.deepEqual(json('cursors'), [3080,5909]);
assert.equal(element('#measurementReadout').textContent, 'Δt: 0.736719 s');
run('zoomAtCenter(0.6); setChartMode("axes")'); assert.deepEqual(json('cursors'), [3080,5909]);
run('setCursor(0,-1);setCursor(1,100)'); assert.deepEqual(json('cursors'), [0,19199]);
element('#cursorAInput').value = '0.5'; fire('#cursorAInput', 'input'); assert.equal(run('cursors[0]'),1920);
assert.equal(element('#cursorAInput').value,'0.5');
fire('#cursorAInput','change'); assert.equal(element('#cursorAInput').value,'0.500000');
run('measuring=true;cursors=[null,null];resetZoom()');
const canvas = '#captureChart';
const pointer = (x,y=150) => ({clientX:x,clientY:y,button:0,pointerId:1});
fire(canvas,'pointerdown',pointer(150)); fire(canvas,'pointerup',pointer(150));
fire(canvas,'pointerdown',pointer(350)); fire(canvas,'pointermove',pointer(400)); fire(canvas,'pointerup',pointer(400));
assert.deepEqual(json('cursors'),[1912,7106]);
run('measuring=false;setView(1,1)');
fire(canvas,'pointerdown',pointer(400)); fire(canvas,'pointermove',pointer(492)); fire(canvas,'pointerup',pointer(492));
assert.ok(Math.abs(run('viewStart')-(1-92/924))<1e-12);
const domain = json('currentDomain()');
fire(canvas,'wheel',{...pointer(500),deltaY:-1,preventDefault(){}});
assert.equal(run('viewSeconds'),0.8); assert.deepEqual(json('currentDomain()'),domain);
run('displayCapture(bytes)'); assert.deepEqual(json('cursors'),[null,null]); assert.equal(run('verticalScale'),'auto');
// Beam data uses the same format, with its own rate, scale, and trigger flag.
const beamBytes = new Uint8Array(32 + 7200 * 6);
const beamHeader = new DataView(beamBytes.buffer);
beamBytes.set([0x45,0x47,0x47,0x31,1,9]);
beamHeader.setUint16(6,32,true); beamHeader.setUint32(8,480,true);
beamHeader.setUint32(12,7200,true); beamHeader.setUint32(16,240,true);
beamHeader.setUint32(20,6959,true); beamHeader.setUint16(24,16384,true);
beamHeader.setUint16(26,6,true);
for (let i=0;i<7200;i++) {
  beamHeader.setInt16(32+i*6,Math.round(3000*Math.sin(2*Math.PI*0.6*i/480)),true);
  beamHeader.setInt16(32+i*6+4,16384,true);
}
sandbox.beamBytes=beamBytes;
beamHeader.setUint32(28,run('crc32(beamBytes.slice(32))'),true);
run('displayCapture(beamBytes)');
assert.equal(run('capture.validCrc'),true);
assert.equal(run('captureSeconds()'),15);
assert.equal(run('capture.z[0]'),1);
assert.equal(run('capture.triggerOffset/capture.sampleRate'),0.5);
assert.equal(element('#triggerType').textContent,'Beam flick');
assert.equal(run('detectFlight()'),undefined);
assert.ok(json('detectEvents()').every(event=>!event.label.includes('Freefall')&&!event.label.includes('impact')));
console.log(`PASS: ${files} real captures; overview, zoom, scales, cursors, controls, reset; Beam timing, scale, CRC, and event labels.`);
