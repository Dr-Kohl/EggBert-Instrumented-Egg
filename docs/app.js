"use strict";

const HEADER_BYTES = 32, MAGIC = [0x45, 0x47, 0x47, 0x31];
const FLAG_TRIGGERED = 1, FLAG_FREEFALL = 2, FLAG_FIFO_OVERRUN = 4;
let port, activeReader, capture, captureBytes, showLabels = true, orientationActive = false, orientationView;
let viewStart = 0, viewSeconds = 0, drag, chartMode = "axes";
const $ = selector => document.querySelector(selector);
const setStatus = text => { $("#connectionStatus").textContent = text; };
const setError = (text = "") => { $("#errorStatus").textContent = text; };

function concat(first, second) { const all = new Uint8Array(first.length + second.length); all.set(first); all.set(second, first.length); return all; }
function findMagic(bytes) { for (let i = 0; i <= bytes.length - 4; i += 1) if (MAGIC.every((value, offset) => bytes[i + offset] === value)) return i; return -1; }
function crc32(bytes) { let crc = 0xffffffff; for (const value of bytes) { crc ^= value; for (let bit = 0; bit < 8; bit += 1) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0); } return (~crc) >>> 0; }

function parseEgg(bytes) {
  if (bytes.length < HEADER_BYTES || !MAGIC.every((value, i) => bytes[i] === value)) throw new Error("This is not an EggBert .egg file.");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const version = view.getUint8(4), flags = view.getUint8(5), headerBytes = view.getUint16(6, true);
  const sampleRate = view.getUint32(8, true), sampleCount = view.getUint32(12, true), triggerOffset = view.getUint32(16, true), postSamples = view.getUint32(20, true);
  const countsPerG = view.getUint16(24, true), sampleBytes = view.getUint16(26, true), expectedCrc = view.getUint32(28, true);
  if (version !== 1 || headerBytes !== HEADER_BYTES || sampleBytes !== 6 || countsPerG === 0) throw new Error("Unsupported EggBert file format.");
  const totalBytes = headerBytes + sampleCount * sampleBytes;
  if (bytes.length !== totalBytes) throw new Error(`Incomplete capture: expected ${totalBytes} bytes, received ${bytes.length}.`);
  const x = new Float32Array(sampleCount), y = new Float32Array(sampleCount), z = new Float32Array(sampleCount), magnitude = new Float32Array(sampleCount);
  for (let i = 0; i < sampleCount; i += 1) { const offset = headerBytes + i * sampleBytes; x[i] = view.getInt16(offset, true) / countsPerG; y[i] = view.getInt16(offset + 2, true) / countsPerG; z[i] = view.getInt16(offset + 4, true) / countsPerG; magnitude[i] = Math.hypot(x[i], y[i], z[i]); }
  return { flags, sampleRate, sampleCount, triggerOffset, postSamples, x, y, z, magnitude, validCrc: crc32(bytes.slice(headerBytes)) === expectedCrc };
}

function detectFlight() {
  if (!capture) return undefined;
  const freefallThreshold = 0.4, impactThreshold = 4;
  const minimumFreefallSamples = Math.max(1, Math.round(capture.sampleRate * 0.08));
  const minimumFlightSamples = Math.max(1, Math.round(capture.sampleRate * 0.3));
  for (let startIndex = 0; startIndex <= capture.sampleCount - minimumFreefallSamples; startIndex += 1) {
    let sustainedFreefall = true;
    for (let index = startIndex; index < startIndex + minimumFreefallSamples; index += 1)
      if (capture.magnitude[index] >= freefallThreshold) { sustainedFreefall = false; break; }
    if (!sustainedFreefall) continue;
    for (let impactIndex = startIndex + minimumFlightSamples; impactIndex < capture.sampleCount; impactIndex += 1)
      if (capture.magnitude[impactIndex] > impactThreshold) return { startIndex, impactIndex };
  }
  return undefined;
}

function displayCapture(bytes) {
  capture = parseEgg(bytes); captureBytes = bytes;
  chartMode = "axes";
  resetZoom();
  $("#summary").hidden = false; $("#charts").hidden = false;
  $("#sampleCount").textContent = `${capture.sampleCount.toLocaleString()} samples`;
  $("#sampleRate").textContent = `${capture.sampleRate.toLocaleString()} Hz`;
  const trigger = (capture.flags & FLAG_TRIGGERED) ? ((capture.flags & FLAG_FREEFALL) ? "Freefall" : "Impact") : "No event";
  $("#triggerType").textContent = trigger + ((capture.flags & FLAG_FIFO_OVERRUN) ? " · FIFO overrun" : "");
  const flight = detectFlight();
  if (flight) {
    const seconds = (flight.impactIndex - flight.startIndex) / capture.sampleRate;
    const heightMeters = 9.80665 * seconds * seconds / 8;
    $("#flightTime").textContent = `${seconds.toFixed(3)} s`;
    $("#peakHeight").textContent = `${heightMeters.toFixed(2)} m (${(heightMeters * 3.28084).toFixed(1)} ft)`;
  } else {
    $("#flightTime").textContent = "Not detected";
    $("#peakHeight").textContent = "—";
  }
  $("#integrity").textContent = capture.validCrc ? "CRC verified" : "CRC FAILED";
  $("#integrity").style.color = capture.validCrc ? "" : "#b00020";
  $("#saveButton").disabled = false; $("#saveCsvButton").disabled = false; drawAll();
}

function captureSeconds() { return capture ? capture.sampleCount / capture.sampleRate : 0; }
function clamp(value, minimum, maximum) { return Math.max(minimum, Math.min(maximum, value)); }
function updateZoomControl() {
  const ready = !!capture;
  ["#axesButton", "#magnitudeButton", "#showLabels", "#zoomInButton", "#zoomOutButton", "#panEarlierButton", "#panLaterButton", "#focusEventButton"].forEach(selector => { $(selector).disabled = !ready; });
  $("#axesButton").setAttribute("aria-pressed", String(chartMode === "axes"));
  $("#magnitudeButton").setAttribute("aria-pressed", String(chartMode === "magnitude"));
  $("#showLabels").setAttribute("aria-pressed", String(showLabels));
  $("#resetZoomButton").disabled = !ready || (viewStart === 0 && viewSeconds === captureSeconds());
}
function resetZoom() { viewStart = 0; viewSeconds = captureSeconds(); updateZoomControl(); }
function setView(start, seconds) {
  const total = captureSeconds();
  viewSeconds = clamp(seconds, Math.min(0.02, total), total);
  viewStart = clamp(start, 0, total - viewSeconds);
  updateZoomControl(); drawAll();
}
function zoomAt(ratio, factor) {
  const total = captureSeconds();
  const nextSeconds = clamp(viewSeconds * factor, Math.min(0.02, total), total);
  const anchor = viewStart + ratio * viewSeconds;
  setView(anchor - ratio * nextSeconds, nextSeconds);
}
function zoomAtCenter(factor) { zoomAt(0.5, factor); }
function panView(direction) { setView(viewStart + direction * viewSeconds * 0.5, viewSeconds); }
function setChartMode(mode) { chartMode = mode; updateZoomControl(); drawAll(); }
function focusEvent() {
  const total = captureSeconds();
  const windowSeconds = Math.min(2, total);
  let eventIndex = capture.triggerOffset !== 0xffffffff && capture.triggerOffset < capture.sampleCount ? capture.triggerOffset : capture.magnitude.findIndex(value => value < 0.25);
  if (eventIndex < 0) eventIndex = Math.round(capture.sampleCount / 2);
  setView(eventIndex / capture.sampleRate - 0.25, windowSeconds);
}

function drawAll() {
  if (!capture) return;
  const events = detectEvents();
  if (chartMode === "axes") {
    $("#chartTitle").textContent = "Acceleration";
    $("#chartLegend").hidden = false;
    $("#captureChart").setAttribute("aria-label", "Acceleration X, Y, and Z in g over time. Scroll to zoom and drag to pan.");
    drawPlot($("#captureChart"), [capture.x, capture.y, capture.z], [-16, 16], ["#c33", "#17834d", "#2463c5"], "g", events);
  } else {
    $("#chartTitle").textContent = "Acceleration magnitude";
    $("#chartLegend").hidden = true;
    $("#captureChart").setAttribute("aria-label", "Acceleration magnitude in g over time. Scroll to zoom and drag to pan.");
    const maximumMagnitude = capture.magnitude.reduce((maximum, value) => Math.max(maximum, value), 2);
    drawPlot($("#captureChart"), [capture.magnitude], [0, Math.ceil(maximumMagnitude)], ["#6240a0"], "g", events);
  }
}

function detectEvents() {
  const events = [];
  const add = (index, label, color) => { if (index < 0 || index >= capture.sampleCount) return; const nearby = events.find(event => Math.abs(event.index - index) < Math.max(1, capture.sampleRate * 0.04)); if (nearby) { if (!nearby.label.includes(label)) nearby.label += " / " + label; if (color === "#b00020") nearby.color = color; } else events.push({ index, label, color }); };
  if (capture.triggerOffset !== 0xffffffff && capture.triggerOffset < capture.sampleCount) add(capture.triggerOffset, "Trigger", "#7a5f00");
  let peakIndex = 0, peak = 0;
  for (let i = 0; i < capture.sampleCount; i += 1) {
    if (capture.magnitude[i] > peak) { peak = capture.magnitude[i]; peakIndex = i; }
    if (Math.abs(capture.x[i]) >= 15.9 || Math.abs(capture.y[i]) >= 15.9 || Math.abs(capture.z[i]) >= 15.9) add(i, "Saturation", "#b00020");
  }
  const flight = detectFlight();
  if (flight) {
    add(flight.startIndex, "Flight start", "#2463c5");
    add(flight.impactIndex, "Impact detected", "#b00020");
  } else {
    const freefallIndex = capture.magnitude.findIndex(value => value < 0.25);
    if (freefallIndex >= 0) add(freefallIndex, "Freefall", "#2463c5");
  }
  if (peak > 2) add(peakIndex, "Peak impact", "#b00020");
  if (events.length === 0 || peak <= 2) add(0, "Gravity baseline", "#6240a0");
  return events.sort((a, b) => a.index - b.index);
}

function drawPlot(canvas, series, domain, colors, unit, events) {
  const ctx = canvas.getContext("2d"), width = canvas.width, height = canvas.height, left = 58, right = 18, top = 18, bottom = 32;
  const plotWidth = width - left - right, plotHeight = height - top - bottom;
  const startIndex = Math.max(0, Math.floor(viewStart * capture.sampleRate));
  const endIndex = Math.min(capture.sampleCount - 1, Math.ceil((viewStart + viewSeconds) * capture.sampleRate));
  const timeAt = index => index / capture.sampleRate;
  const xAt = index => left + plotWidth * (timeAt(index) - viewStart) / viewSeconds;
  ctx.clearRect(0, 0, width, height); ctx.font = "15px system-ui"; ctx.fillStyle = "#555"; ctx.strokeStyle = "#c8c8c8"; ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i += 1) { const y = top + plotHeight * i / 4, value = domain[1] - (domain[1] - domain[0]) * i / 4; ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(width - right, y); ctx.stroke(); ctx.fillText(`${value.toFixed(1)} ${unit}`, 3, y + 5); }
  const precision = viewSeconds < 1 ? 2 : 1;
  for (let i = 0; i <= 5; i += 1) { const x = left + plotWidth * i / 5; ctx.fillText(`${(viewStart + viewSeconds * i / 5).toFixed(precision)} s`, x - 12, height - 8); }
  if (showLabels) events.filter(event => event.index >= startIndex && event.index <= endIndex).forEach((event, eventIndex) => { const x = xAt(event.index); ctx.strokeStyle = event.color; ctx.lineWidth = 1.5; ctx.setLineDash([5, 4]); ctx.beginPath(); ctx.moveTo(x, top); ctx.lineTo(x, height - bottom); ctx.stroke(); ctx.setLineDash([]); ctx.save(); ctx.font = "bold 13px system-ui"; const labelWidth = ctx.measureText(event.label).width; const row = eventIndex % 3; const labelY = top + 15 + row * 17; const labelX = Math.min(width - labelWidth - 5, Math.max(left + 5, x - labelWidth / 2)); ctx.fillStyle = "rgba(255,255,255,0.9)"; ctx.fillRect(labelX - 3, labelY - 13, labelWidth + 6, 17); ctx.fillStyle = event.color; ctx.fillText(event.label, labelX, labelY); ctx.beginPath(); ctx.moveTo(x, labelY + 4); ctx.lineTo(x, top + 2); ctx.stroke(); ctx.restore(); });
  const visibleSamples = endIndex - startIndex + 1;
  const showSamples = visibleSamples <= plotWidth / 3;
  series.forEach((values, index) => {
    ctx.strokeStyle = colors[index]; ctx.lineWidth = 1.15; ctx.beginPath();
    const stride = Math.max(1, Math.ceil(visibleSamples / plotWidth));
    for (let i = startIndex; i <= endIndex; i += stride) { const x = xAt(i), y = top + plotHeight * (domain[1] - values[i]) / (domain[1] - domain[0]); if (i === startIndex) ctx.moveTo(x, y); else ctx.lineTo(x, y); }
    ctx.stroke();
    if (showSamples) {
      ctx.fillStyle = colors[index]; ctx.beginPath();
      for (let i = startIndex; i <= endIndex; i += 1) { const y = top + plotHeight * (domain[1] - values[i]) / (domain[1] - domain[0]); ctx.moveTo(xAt(i) + 2, y); ctx.arc(xAt(i), y, 2, 0, Math.PI * 2); }
      ctx.fill();
    }
  });
}

function addViewportControls(canvas) {
  canvas.addEventListener("wheel", event => {
    if (!capture) return;
    event.preventDefault();
    const rect = canvas.getBoundingClientRect(), x = (event.clientX - rect.left) * canvas.width / rect.width;
    const ratio = clamp((x - 58) / (canvas.width - 58 - 18), 0, 1);
    zoomAt(ratio, event.deltaY < 0 ? 0.8 : 1.25);
  }, { passive: false });
  canvas.addEventListener("pointerdown", event => {
    if (!capture || event.button !== 0) return;
    drag = { canvas, x: event.clientX, start: viewStart };
    canvas.setPointerCapture(event.pointerId);
  });
  canvas.addEventListener("pointermove", event => {
    if (!drag || drag.canvas !== canvas) return;
    const rect = canvas.getBoundingClientRect(), pixels = (event.clientX - drag.x) * canvas.width / rect.width;
    const seconds = pixels * viewSeconds / (canvas.width - 58 - 18);
    setView(drag.start - seconds, viewSeconds);
  });
  const stopDragging = event => { if (drag && drag.canvas === canvas) { if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId); drag = undefined; } };
  canvas.addEventListener("pointerup", stopDragging); canvas.addEventListener("pointercancel", stopDragging);
}

async function connect() {
  if (!("serial" in navigator)) throw new Error("Web Serial is unavailable. Use Chrome or Edge over HTTPS, or open a saved .egg file.");
  port = await navigator.serial.requestPort(); await port.open({ baudRate: 115200, bufferSize: 65536 });
  $("#connectButton").textContent = "EggBert connected"; $("#connectButton").disabled = true; $("#downloadButton").disabled = false; $("#orientation").hidden = false; $("#orientationButton").disabled = false; setStatus("EggBert connected. Complete the test using EggBert's buttons, then download it here.");
}

async function disconnect() {
  if (activeReader) await activeReader.cancel();
  if (port) await port.close();
  port = undefined; activeReader = undefined;
  $("#connectButton").textContent = "Connect EggBert"; $("#connectButton").disabled = false; $("#downloadButton").disabled = true;
  setStatus("Device disconnected.");
}

async function downloadCapture() {
  if (!port) return; setError(""); setStatus("Requesting capture…"); $("#downloadButton").disabled = true;
  const writer = port.writable.getWriter(); await writer.write(new TextEncoder().encode("download\n")); writer.releaseLock();
  const reader = port.readable.getReader(); activeReader = reader; let received = new Uint8Array(), required = null, start = -1, verified = false;
  try {
    while (required === null || received.length - start < required) {
      const result = await Promise.race([reader.read(), new Promise(resolve => setTimeout(() => resolve({ timeout: true }), 10000))]);
      if (result.timeout) { await reader.cancel(); throw new Error("EggBert did not start a binary download. Confirm CAPTURE COMPLETE, then try again."); }
      const { value, done } = result; if (done) throw new Error("EggBert disconnected during transfer."); received = concat(received, value);
      if (start < 0) start = findMagic(received);
      if (start < 0) { const text = new TextDecoder().decode(received); if (text.includes("ERROR:")) throw new Error(text.trim()); }
      if (start >= 0 && received.length >= start + HEADER_BYTES) { const view = new DataView(received.buffer, received.byteOffset + start, HEADER_BYTES); required = view.getUint16(6, true) + view.getUint32(12, true) * view.getUint16(26, true); setStatus(`Downloading ${required.toLocaleString()} bytes…`); }
    }
    displayCapture(received.slice(start, start + required));
    if (!capture.validCrc) throw new Error("Capture CRC verification failed. EggBert remains connected so you can retry the download.");
    verified = true;
  } finally { if (activeReader === reader) activeReader = undefined; reader.releaseLock(); $("#downloadButton").disabled = false; }
  if (verified) {
    try {
      await disconnect();
      setStatus("Capture received and verified. USB disconnected; save the .egg file, then erase EggBert when ready.");
    } catch (error) {
      setError("Capture received and verified, but USB did not release automatically. Unplug and reconnect EggBert before another transfer.");
      setStatus("Capture received and verified.");
    }
  }
}

$("#connectButton").addEventListener("click", () => connect().catch(error => { setError(error.message); setStatus("No device connected."); }));
$("#downloadButton").addEventListener("click", () => downloadCapture().catch(error => { setError(error.message); setStatus("Download did not complete."); }));
function makeOrientationView() {
  if (orientationView) return orientationView;
  if (!window.THREE) throw new Error("The 3D viewer library did not load. Check the internet connection and reload this page.");
  const host = $("#orientationCanvas"), scene = new THREE.Scene(), camera = new THREE.PerspectiveCamera(34, 1, .1, 100);
  const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2)); renderer.shadowMap.enabled = true; renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  host.replaceChildren(renderer.domElement); scene.background = new THREE.Color(0xe8edf0); camera.position.set(4.8, 3.5, 6.8); camera.lookAt(0, .2, 0);
  const light = new THREE.DirectionalLight(0xffffff, 2.4); light.position.set(4, 6, 5); light.castShadow = true; scene.add(light, new THREE.HemisphereLight(0xcfe4ff, 0x4b5560, 1.4));
  const floor = new THREE.Mesh(new THREE.PlaneGeometry(30, 30), new THREE.ShadowMaterial({ color: 0x223344, opacity: .18 })); floor.rotation.x = -Math.PI / 2; floor.position.y = -1.05; floor.receiveShadow = true; scene.add(floor);
  const orbit = new THREE.Group(), sensorFrame = new THREE.Group(); orbit.add(sensorFrame); scene.add(orbit);
  const outline = [[-1.28,-.92],[1.28,-.92],[1.58,-.62],[1.58,.62],[1.28,.92],[-1.28,.92],[-1.58,.62],[-1.58,-.62]];
  const shape = new THREE.Shape(); outline.forEach(([x,z], i) => i ? shape.lineTo(x, z) : shape.moveTo(x, z)); shape.closePath();
  const bodyGeometry = new THREE.ExtrudeGeometry(shape, { depth: .86, bevelEnabled: true, bevelSegments: 3, bevelSize: .075, bevelThickness: .075 }); bodyGeometry.rotateX(Math.PI / 2); bodyGeometry.translate(0, .43, 0);
  const body = new THREE.Mesh(bodyGeometry, new THREE.MeshStandardMaterial({ color: 0xd9d0b6, roughness: .65, metalness: .05 })); body.castShadow = body.receiveShadow = true; sensorFrame.add(body);
  const screenFrame = new THREE.Mesh(new THREE.BoxGeometry(1.45, .08, .88), new THREE.MeshStandardMaterial({ color: 0x25282a, roughness: .55 })); screenFrame.position.set(0, .08, .96); sensorFrame.add(screenFrame);
  const screen = new THREE.Mesh(new THREE.PlaneGeometry(1.25, .67), new THREE.MeshStandardMaterial({ color: 0x263a44, roughness: .28, metalness: .22 })); screen.position.set(0, .08, 1.01); sensorFrame.add(screen);
  const buttonMaterial = new THREE.MeshStandardMaterial({ color: 0x343536, roughness: .4, metalness: .35 });
  [[-.72,.12],[0,.3],[.72,.12]].forEach(([x,z]) => { const button = new THREE.Mesh(new THREE.CylinderGeometry(.17, .17, .085, 28), buttonMaterial); button.position.set(x, .51, z); button.castShadow = true; sensorFrame.add(button); });
  const usb = new THREE.Mesh(new THREE.BoxGeometry(.08, .28, .42), new THREE.MeshStandardMaterial({ color: 0x303438, metalness: .55, roughness: .28 })); usb.position.set(1.62, -.1, 0); sensorFrame.add(usb);
  const label = new THREE.Sprite(new THREE.SpriteMaterial({ map: new THREE.CanvasTexture(Object.assign(document.createElement("canvas"), { width: 480, height: 96 })) }));
  const labelContext = label.material.map.image.getContext("2d"); labelContext.fillStyle="#1d2930"; labelContext.font="bold 44px sans-serif"; labelContext.textAlign="center"; labelContext.fillText("+Y button side", 240, 58); label.material.map.needsUpdate = true; label.scale.set(2.6, .52, 1); label.position.set(0, .98, 0); sensorFrame.add(label);
  renderer.domElement.addEventListener("wheel", event => { camera.position.multiplyScalar(event.deltaY > 0 ? 1.08 : .92); camera.position.clampLength(4, 10); camera.lookAt(0, .2, 0); event.preventDefault(); }, { passive: false });
  const render = () => { const width = host.clientWidth, height = host.clientHeight; if (renderer.domElement.width !== Math.round(width * renderer.getPixelRatio()) || renderer.domElement.height !== Math.round(height * renderer.getPixelRatio())) { renderer.setSize(width, height, false); camera.aspect = width / height; camera.updateProjectionMatrix(); } renderer.render(scene, camera); requestAnimationFrame(render); }; render();
  orientationView = { sensorFrame }; return orientationView;
}
async function setOrientation(on) { if (!port) return; if (on) makeOrientationView(); const w=port.writable.getWriter(); await w.write(new TextEncoder().encode(`orientation ${on?"on":"off"}\n`)); w.releaseLock(); orientationActive=on; $("#orientationButton").disabled=on; $("#orientationStopButton").disabled=!on; $("#downloadButton").disabled=on; if(!on){if(activeReader) await activeReader.cancel(); return;} const r=port.readable.getReader(); activeReader=r; let b="",d=new TextDecoder(); try{while(orientationActive){const q=await r.read();if(q.done)break;b+=d.decode(q.value,{stream:true});const ls=b.split("\n");b=ls.pop();for(const l of ls){const m=/^O,(-?\d+),(-?\d+),(-?\d+)$/.exec(l.trim());if(!m)continue;const x=+m[1],y=+m[2],z=+m[3],p=Math.atan2(x,Math.hypot(y,z))*180/Math.PI,roll=Math.atan2(z,Math.hypot(x,y))*180/Math.PI;orientationView.sensorFrame.rotation.set(roll*Math.PI/180,0,-p*Math.PI/180);$("#orientationStatus").textContent=`Pitch ${p.toFixed(1)}° · Roll ${roll.toFixed(1)}°`;}}}finally{if(activeReader===r)activeReader=undefined;r.releaseLock(); $("#downloadButton").disabled = false;}}
$("#orientationButton").addEventListener("click",()=>setOrientation(true).catch(e=>setError(e.message)));$("#orientationStopButton").addEventListener("click",()=>setOrientation(false).catch(e=>setError(e.message)));
$("#fileInput").addEventListener("change", async event => { try { setError(""); displayCapture(new Uint8Array(await event.target.files[0].arrayBuffer())); setStatus("Capture file opened."); } catch (error) { setError(error.message); } event.target.value = ""; });
$("#sampleSelect").addEventListener("change", async event => { const filename = event.target.value; if (!filename) return; try { setError(""); setStatus("Loading sample capture…"); const response = await fetch("sample-data/" + filename); if (!response.ok) throw new Error("Could not load sample capture (" + response.status + ")."); displayCapture(new Uint8Array(await response.arrayBuffer())); setStatus("Sample capture opened."); } catch (error) { setError(error.message); setStatus("Sample did not load."); } event.target.value = ""; });
$("#showLabels").addEventListener("click", event => { showLabels = !showLabels; event.currentTarget.setAttribute("aria-pressed", String(showLabels)); drawAll(); });
$("#axesButton").addEventListener("click", () => setChartMode("axes"));
$("#magnitudeButton").addEventListener("click", () => setChartMode("magnitude"));
$("#zoomInButton").addEventListener("click", () => zoomAtCenter(0.6));
$("#zoomOutButton").addEventListener("click", () => zoomAtCenter(1 / 0.6));
$("#panEarlierButton").addEventListener("click", () => panView(-1));
$("#panLaterButton").addEventListener("click", () => panView(1));
$("#focusEventButton").addEventListener("click", focusEvent);
$("#resetZoomButton").addEventListener("click", () => { resetZoom(); drawAll(); });
function downloadFile(blob, filename) {
  const url = URL.createObjectURL(blob);
  const link = Object.assign(document.createElement("a"), { href: url, download: filename });
  link.click();
  URL.revokeObjectURL(url);
}

function suggestedFilename(extension) {
  const timestamp = new Date().toISOString().replace(/[-:]/g, "").replace("T", "-").replace(/\.\d{3}Z$/, "");
  return `eggbert-capture-${timestamp}.${extension}`;
}

async function saveCaptureFile(blob, extension, description, mimeType) {
  const filename = suggestedFilename(extension);
  if (!("showSaveFilePicker" in window)) {
    downloadFile(blob, filename);
    setStatus("Your browser does not provide a Save As dialog; the download used the suggested filename.");
    return;
  }
  const handle = await window.showSaveFilePicker({
    suggestedName: filename,
    types: [{ description, accept: { [mimeType]: [`.${extension}`] } }],
  });
  const writable = await handle.createWritable();
  await writable.write(blob);
  await writable.close();
  setStatus(`Saved ${handle.name}.`);
}

async function exportCsv() {
  if (!capture) return;
  const rows = ["Time (s),X (g),Y (g),Z (g),Magnitude (g)"];
  for (let index = 0; index < capture.sampleCount; index += 1) {
    rows.push([
      (index / capture.sampleRate).toFixed(6),
      capture.x[index].toFixed(6),
      capture.y[index].toFixed(6),
      capture.z[index].toFixed(6),
      capture.magnitude[index].toFixed(6),
    ].join(","));
  }
  await saveCaptureFile(new Blob([rows.join("\r\n") + "\r\n"], { type: "text/csv;charset=utf-8" }), "csv", "CSV files", "text/csv");
}

$("#saveButton").addEventListener("click", () => saveCaptureFile(new Blob([captureBytes], { type: "application/octet-stream" }), "egg", "EggBert capture files", "application/octet-stream").catch(error => { if (error.name !== "AbortError") setError(`Save failed: ${error.message}`); }));
$("#saveCsvButton").addEventListener("click", () => exportCsv().catch(error => { if (error.name !== "AbortError") setError(`CSV export failed: ${error.message}`); }));
addViewportControls($("#captureChart"));
window.addEventListener("resize", drawAll);
