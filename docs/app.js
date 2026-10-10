"use strict";

const HEADER_BYTES = 32, MAGIC = [0x45, 0x47, 0x47, 0x31];
const FLAG_TRIGGERED = 1, FLAG_FREEFALL = 2, FLAG_FIFO_OVERRUN = 4;
let port, activeReader, capture, captureBytes, showLabels = true, orientationActive = false, orientationView;
let orientationHeading = 0, orientationLatest, orientationReady = false;
let viewStart = 0, viewSeconds = 0, drag, chartMode = "axes";
let verticalScale = "auto", fittedDomains, fullDomains;
let measuring = false, cursors = [null, null];
const PLOT = { left: 58, right: 18, top: 18, bottom: 32 };
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
  verticalScale = "auto"; fittedDomains = undefined;
  fullDomains = { axes: domainFor([capture.x, capture.y, capture.z], 0, capture.sampleCount - 1, true), magnitude: domainFor([capture.magnitude], 0, capture.sampleCount - 1, false) };
  $("#verticalScale").value = "auto";
  measuring = false; cursors = [null, null]; drag = undefined;
  resetZoom(); updateMeasurement();
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
  ["#axesButton", "#magnitudeButton", "#showLabels", "#focusEventButton", "#verticalScale", "#fitVisibleButton", "#measureButton"].forEach(selector => { $(selector).disabled = !ready; });
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

function niceLimit(maximum) {
  const padded = Math.max(0.1, maximum * 1.08);
  const power = 10 ** Math.floor(Math.log10(padded));
  return [1, 2, 4, 5, 8, 10].find(step => step * power >= padded) * power;
}
function domainFor(series, start, end, symmetric) {
  let maximum = 0;
  for (const values of series) for (let i = start; i <= end; i += 1) maximum = Math.max(maximum, Math.abs(values[i]));
  const limit = niceLimit(maximum);
  return symmetric ? [-limit, limit] : [0, limit];
}
function visibleIndices() {
  return [Math.max(0, Math.floor(viewStart * capture.sampleRate)), Math.min(capture.sampleCount - 1, Math.ceil((viewStart + viewSeconds) * capture.sampleRate))];
}
function currentDomain() {
  if (verticalScale === "auto") return fullDomains[chartMode];
  if (verticalScale === "fit") return fittedDomains[chartMode];
  const limit = Number(verticalScale);
  return chartMode === "axes" ? [-limit, limit] : [0, limit];
}
function fitVisibleData() {
  if (!capture) return;
  const [start, end] = visibleIndices();
  fittedDomains = { axes: domainFor([capture.x, capture.y, capture.z], start, end, true), magnitude: domainFor([capture.magnitude], start, end, false) };
  verticalScale = "fit"; $("#verticalScale").value = "fit"; drawAll();
}
function drawAll() {
  if (!capture) return;
  const events = detectEvents(), domain = currentDomain();
  $("#scaleReadout").textContent = `${domain[0]} to ${domain[1]} g`;
  for (const option of $("#verticalScale").options) if (["2", "4", "8", "16"].includes(option.value)) option.textContent = chartMode === "axes" ? `±${option.value} g` : `0–${option.value} g`;
  const axes = chartMode === "axes";
  $("#chartTitle").textContent = axes ? "Acceleration" : "Acceleration magnitude";
  $("#chartLegend").hidden = !axes;
  $("#captureChart").setAttribute("aria-label", `${axes ? "Acceleration X, Y, and Z" : "Acceleration magnitude"} in g over time. Scroll to zoom. ${measuring ? "Click to place and drag measurement cursors." : "Drag to pan."}`);
  drawPlot($("#captureChart"), axes ? [capture.x, capture.y, capture.z] : [capture.magnitude], domain, axes ? ["#c33", "#17834d", "#2463c5"] : ["#6240a0"], "g", events);
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
  ctx.save(); ctx.beginPath(); ctx.rect(left, top, plotWidth, plotHeight); ctx.clip();
  series.forEach((values, index) => {
    ctx.strokeStyle = colors[index]; ctx.lineWidth = 1.15; ctx.beginPath();
    const indices = peakPreservingIndices(values, startIndex, endIndex, plotWidth);
    for (const [position, i] of indices.entries()) { const x = xAt(i), y = top + plotHeight * (domain[1] - values[i]) / (domain[1] - domain[0]); if (position === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y); }
    ctx.stroke();
    if (showSamples) {
      ctx.fillStyle = colors[index]; ctx.beginPath();
      for (let i = startIndex; i <= endIndex; i += 1) { const y = top + plotHeight * (domain[1] - values[i]) / (domain[1] - domain[0]); ctx.moveTo(xAt(i) + 2, y); ctx.arc(xAt(i), y, 2, 0, Math.PI * 2); }
      ctx.fill();
    }
  });
  ctx.restore();
  drawMeasurementCursors(ctx, canvas);
}

// Retain extrema and endpoints in chronological order in every display bucket.
// At close zoom the bucket size is one, so every original sample is drawn.
function peakPreservingIndices(values, start, end, plotWidth) {
  const stride = Math.max(1, Math.ceil((end - start + 1) / plotWidth)), indices = [];
  for (let first = start; first <= end; first += stride) {
    const last = Math.min(end, first + stride - 1);
    let minimum = first, maximum = first;
    for (let i = first + 1; i <= last; i += 1) { if (values[i] < values[minimum]) minimum = i; if (values[i] > values[maximum]) maximum = i; }
    indices.push(...[...new Set([first, minimum, maximum, last])].sort((a, b) => a - b));
  }
  return indices;
}
function updateMeasurement(syncInputs = true) {
  const ready = !!capture;
  $("#measurementPanel").hidden = !measuring;
  $("#measureButton").setAttribute("aria-pressed", String(measuring));
  $("#captureChart").classList.toggle("measuring", measuring);
  $("#clearMeasurementButton").disabled = !ready || cursors.every(index => index === null);
  for (const [i, id] of ["#cursorAInput", "#cursorBInput"].entries()) {
    $(id).disabled = !ready;
    $(id).max = ready ? String((capture.sampleCount - 1) / capture.sampleRate) : "0";
    if (syncInputs) $(id).value = cursors[i] === null ? "" : (cursors[i] / capture.sampleRate).toFixed(6);
  }
  $("#measurementReadout").textContent = cursors[0] === null ? "Choose point A." : cursors[1] === null ? "Choose point B." : `Δt: ${(Math.abs(cursors[1] - cursors[0]) / capture.sampleRate).toFixed(6)} s`;
}
function setCursor(which, seconds, syncInputs = true) {
  if (!capture || !Number.isFinite(seconds)) return;
  cursors[which] = clamp(Math.round(seconds * capture.sampleRate), 0, capture.sampleCount - 1);
  updateMeasurement(syncInputs); drawAll();
}
function drawMeasurementCursors(ctx, canvas) {
  if (!measuring) return;
  cursors.forEach((index, i) => {
    if (index === null) return;
    const time = index / capture.sampleRate;
    if (time < viewStart || time > viewStart + viewSeconds) return;
    const x = PLOT.left + (canvas.width - PLOT.left - PLOT.right) * (time - viewStart) / viewSeconds;
    ctx.save(); ctx.strokeStyle = ctx.fillStyle = i === 0 ? "#9940aa" : "#b25b00"; ctx.lineWidth = 2; ctx.setLineDash([3, 3]);
    ctx.beginPath(); ctx.moveTo(x, PLOT.top); ctx.lineTo(x, canvas.height - PLOT.bottom); ctx.stroke();
    ctx.setLineDash([]); ctx.font = "bold 16px system-ui"; ctx.fillText(i === 0 ? "A" : "B", Math.min(canvas.width - PLOT.right - 14, x + 5), canvas.height - PLOT.bottom - 8); ctx.restore();
  });
}
function canvasPosition(canvas, event) {
  const rect = canvas.getBoundingClientRect();
  return { x: (event.clientX - rect.left) * canvas.width / rect.width, y: (event.clientY - rect.top) * canvas.height / rect.height };
}
function timeAtPointer(canvas, event) {
  const { x } = canvasPosition(canvas, event);
  return viewStart + clamp((x - PLOT.left) / (canvas.width - PLOT.left - PLOT.right), 0, 1) * viewSeconds;
}
function addViewportControls(canvas) {
  canvas.addEventListener("wheel", event => {
    if (!capture) return;
    event.preventDefault();
    const { x } = canvasPosition(canvas, event);
    const ratio = clamp((x - PLOT.left) / (canvas.width - PLOT.left - PLOT.right), 0, 1);
    zoomAt(ratio, event.deltaY < 0 ? 0.8 : 1.25);
  }, { passive: false });
  canvas.addEventListener("pointerdown", event => {
    if (!capture || event.button !== 0) return;
    const { x, y } = canvasPosition(canvas, event);
    if (x < PLOT.left || x > canvas.width - PLOT.right || y < PLOT.top || y > canvas.height - PLOT.bottom) return;
    if (measuring) {
      const seconds = timeAtPointer(canvas, event);
      const distances = cursors.map(index => index === null ? Infinity : Math.abs(index / capture.sampleRate - seconds));
      const hitDistance = 12 * viewSeconds / (canvas.width - PLOT.left - PLOT.right);
      let which = distances[0] <= distances[1] ? 0 : 1;
      if (distances[which] > hitDistance && cursors.includes(null)) which = cursors.indexOf(null);
      drag = { canvas, cursor: which }; setCursor(which, seconds);
    } else drag = { canvas, x: event.clientX, start: viewStart };
    canvas.setPointerCapture(event.pointerId);
  });
  canvas.addEventListener("pointermove", event => {
    if (!drag || drag.canvas !== canvas) return;
    if (drag.cursor !== undefined) { setCursor(drag.cursor, timeAtPointer(canvas, event)); return; }
    const rect = canvas.getBoundingClientRect(), pixels = (event.clientX - drag.x) * canvas.width / rect.width;
    setView(drag.start - pixels * viewSeconds / (canvas.width - PLOT.left - PLOT.right), viewSeconds);
  });
  const stopDragging = event => { if (drag && drag.canvas === canvas) { if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId); drag = undefined; } };
  canvas.addEventListener("pointerup", stopDragging); canvas.addEventListener("pointercancel", stopDragging); canvas.addEventListener("lostpointercapture", () => { drag = undefined; });
}

async function connect() {
  if (!("serial" in navigator)) throw new Error("Web Serial is unavailable. Use Chrome or Edge over HTTPS, or open a saved .egg file.");
  port = await navigator.serial.requestPort(); await port.open({ baudRate: 115200, bufferSize: 65536 });
  $("#connectButton").textContent = "EggBert connected"; $("#connectButton").disabled = true; $("#downloadButton").disabled = false; $("#orientation").hidden = false; $("#orientationButton").disabled = false; setStatus("EggBert connected. Complete the test using EggBert's buttons, then download it here.");
  $("#orientationStatus").textContent = "Choose Start orientation with EggBert resting in any position.";
  $("#orientationPose").textContent = "Resting side will be detected automatically.";
}

async function disconnect() {
  orientationActive = false;
  if (activeReader) await activeReader.cancel();
  try { if (port) await port.close(); }
  finally {
    port = undefined; activeReader = undefined;
    $("#orientationRecenterButton").disabled = true;
    $("#connectButton").textContent = "Connect EggBert"; $("#connectButton").disabled = false; $("#downloadButton").disabled = true; $("#orientationButton").disabled = true; $("#orientationStopButton").disabled = true; $("#orientation").hidden = true;
    setStatus("Device disconnected.");
  }
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
async function loadStl(url, color) {
  const response = await fetch(url); if (!response.ok) throw new Error(`Could not load ${url}.`);
  const data = new DataView(await response.arrayBuffer()), triangleCount = data.getUint32(80, true), positions = new Float32Array(triangleCount * 9);
  for (let triangle = 0, offset = 84; triangle < triangleCount; triangle += 1, offset += 50) for (let vertex = 0; vertex < 3; vertex += 1) for (let axis = 0; axis < 3; axis += 1) positions[triangle * 9 + vertex * 3 + axis] = data.getFloat32(offset + 12 + vertex * 12 + axis * 4, true);
  const geometry = new THREE.BufferGeometry(); geometry.setAttribute("position", new THREE.BufferAttribute(positions, 3)); geometry.scale(.045, .045, .045); geometry.computeVertexNormals();
  const mesh = new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({ color, roughness: .62, metalness: .06 })); mesh.castShadow = mesh.receiveShadow = true; return mesh;
}
function makeOrientationView() {
  if (orientationView) return orientationView;
  if (!window.THREE) throw new Error("The 3D viewer library did not load. Check the internet connection and reload this page.");
  const host = $("#orientationCanvas"), scene = new THREE.Scene(), camera = new THREE.PerspectiveCamera(34, 1, .1, 100);
  const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2)); renderer.shadowMap.enabled = true; renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  host.replaceChildren(renderer.domElement); scene.background = new THREE.Color(0xe8edf0); camera.position.set(0, 1.8, -7.5); camera.lookAt(0, 0, 0);
  const light = new THREE.DirectionalLight(0xffffff, 2.4); light.position.set(4, 6, 5); light.castShadow = true; scene.add(light, new THREE.HemisphereLight(0xcfe4ff, 0x4b5560, 1.4));
  const floor = new THREE.Mesh(new THREE.PlaneGeometry(30, 30), new THREE.ShadowMaterial({ color: 0x223344, opacity: .18 })); floor.rotation.x = -Math.PI / 2; floor.position.y = -1.05; floor.receiveShadow = true; scene.add(floor);
  const motionFrame = new THREE.Group(), caseFrame = new THREE.Group();
  // CAD and sensor axes agree: +X USB, +Y buttons, +Z out through the screen.
  // In the +Y-down home pose: buttons are down, screen faces the viewer, USB is left.
  caseFrame.rotation.set(Math.PI, 0, 0); motionFrame.add(caseFrame); scene.add(motionFrame);
  Promise.all([loadStl("models/eggbert-bottom.stl", 0x55514a), loadStl("models/eggbert-top.stl", 0xd9d0b6)]).then(parts => parts.forEach(part => caseFrame.add(part))).catch(error => { $("#orientationStatus").textContent = error.message; });
  renderer.domElement.addEventListener("wheel", event => { camera.position.multiplyScalar(event.deltaY > 0 ? 1.08 : .92); camera.position.clampLength(4, 10); camera.lookAt(0, 0, 0); event.preventDefault(); }, { passive: false });
  const target = new THREE.Quaternion(); let previousFrame;
  const render = time => { const width = host.clientWidth, height = host.clientHeight; if (width && height) { if (renderer.domElement.width !== Math.round(width * renderer.getPixelRatio()) || renderer.domElement.height !== Math.round(height * renderer.getPixelRatio())) { renderer.setSize(width, height, false); camera.aspect = width / height; camera.updateProjectionMatrix(); } const dt = previousFrame === undefined ? 0 : Math.min((time - previousFrame) / 1000, .1); motionFrame.quaternion.slerp(target, 1 - Math.exp(-dt / .045)); renderer.render(scene, camera); } previousFrame = time; requestAnimationFrame(render); }; requestAnimationFrame(render);
  orientationView = { motionFrame, target }; return orientationView;
}
async function sendOrientationCommand(command) {
  const writer = port.writable.getWriter();
  try { await writer.write(new TextEncoder().encode(command + "\n")); }
  finally { writer.releaseLock(); }
}
function recenterOrientation() {
  if (!orientationActive || !orientationLatest || !orientationReady) return;
  orientationHeading = EggOrientation.heading(orientationLatest);
  orientationView.target.set(...EggOrientation.absolute(orientationLatest, orientationHeading));
  orientationView.motionFrame.quaternion.copy(orientationView.target);
  $("#orientationStatus").textContent = "Heading recentered · tilt preserved";
}
async function setOrientation(on) {
  if (!port) return;
  if (on) makeOrientationView();
  if (on) setError("");
  await sendOrientationCommand(on ? "orientation fused" : "orientation off");
  orientationActive = on; $("#orientationButton").disabled = on; $("#orientationStopButton").disabled = !on; $("#downloadButton").disabled = on;
  $("#orientationRecenterButton").disabled = true;
  if (!on) { $("#orientationStatus").textContent = "Stopping and releasing EggBert…"; if (activeReader) await activeReader.cancel(); else await disconnect(); return; }
  orientationHeading = 0; orientationLatest = undefined; orientationReady = false;
  orientationView.target.identity();
  orientationView.motionFrame.quaternion.copy(orientationView.target);
  $("#orientationStatus").textContent = "Hold EggBert still briefly in any resting position…";
  $("#orientationPose").textContent = "Checking resting position…";
  $("#orientationCanvas").setAttribute("aria-busy", "true");
  const reader = port.readable.getReader(); activeReader = reader; let buffer = "", decoder = new TextDecoder();
  let watchdog, timedOut = false, legacy = false, initialized = false;
  const checkStability = EggOrientation.stability();
  const keepAlive = () => { clearTimeout(watchdog); watchdog = setTimeout(() => { timedOut = true; reader.cancel().catch(() => {}); }, 2500); };
  keepAlive();
  try {
    while (orientationActive) {
      const result = await reader.read();
      if (result.done) { if (orientationActive) throw new Error(timedOut ? "Orientation timed out. Return EggBert to HOME and reconnect." : "EggBert disconnected during orientation."); break; }
      buffer += decoder.decode(result.value, { stream: true }); const lines = buffer.split("\n"); buffer = lines.pop();
      if (buffer.length > 4096) throw new Error("Invalid orientation stream. Reconnect EggBert.");
      for (const rawLine of lines) {
        const line = rawLine.trim();
        if (line.startsWith("ERROR:") || line.startsWith("ORIENTATION STOPPED")) throw new Error(line.replace(/^ERROR:\s*/, ""));
        // Older command buffers truncate "orientation fused" to "orientation fus".
        if (line.startsWith("Unknown command: orientation ") && !legacy) { legacy = true; await sendOrientationCommand("orientation on"); keepAlive(); continue; }
        const sample = EggOrientation.parse(line);
        if (sample) {
          keepAlive(); orientationLatest = sample.q;
          const resting = checkStability(sample);
          orientationReady = resting.ready;
          $("#orientationRecenterButton").disabled = !initialized || !resting.ready;
          $("#orientationPose").textContent = resting.still ? `Detected: ${resting.pose}` : "Moving · hold still to check the resting side";
          if (!initialized && resting.ready) {
            orientationHeading = EggOrientation.heading(sample.q); initialized = true;
            $("#orientationRecenterButton").disabled = false;
            $("#orientationCanvas").setAttribute("aria-busy", "false");
          }
          if (initialized) orientationView.target.set(...EggOrientation.absolute(sample.q, orientationHeading));
          $("#orientationStatus").textContent = initialized ? "Gyro + gravity · full 3D rotation" : "Hold EggBert still briefly in any resting position…";
          continue;
        }
        const match = /^O,(-?\d+),(-?\d+),(-?\d+)$/.exec(line);
        if (legacy && match) {
          $("#orientationCanvas").setAttribute("aria-busy", "false");
          keepAlive(); const x = +match[1], y = +match[2], z = +match[3];
          const tilt = EggOrientation.tilt([x,y,z]);
          if (tilt) orientationView.target.set(...tilt);
          $("#orientationPose").textContent = `Tilt estimate: ${EggOrientation.pose([x,y,z])}`;
          $("#orientationStatus").textContent = "Tilt only · update EggBert firmware for gyro rotation";
        }
      }
    }
  } finally {
    clearTimeout(watchdog);
    if (activeReader === reader) activeReader = undefined; reader.releaseLock();
    // Also restore the device after a rejected mode, unplug, or sensor failure.
    if (port) { try { await sendOrientationCommand("orientation off"); } catch (_) {} await disconnect(); }
  }
}
$("#orientationButton").addEventListener("click",()=>setOrientation(true).catch(e=>setError(e.message)));$("#orientationStopButton").addEventListener("click",()=>setOrientation(false).catch(e=>setError(e.message)));
$("#orientationRecenterButton").addEventListener("click",recenterOrientation);
$("#fileInput").addEventListener("change", async event => { try { setError(""); displayCapture(new Uint8Array(await event.target.files[0].arrayBuffer())); setStatus("Capture file opened."); } catch (error) { setError(error.message); } event.target.value = ""; });
$("#sampleSelect").addEventListener("change", async event => { const filename = event.target.value; if (!filename) return; try { setError(""); setStatus("Loading sample capture…"); const response = await fetch("sample-data/" + filename); if (!response.ok) throw new Error("Could not load sample capture (" + response.status + ")."); displayCapture(new Uint8Array(await response.arrayBuffer())); setStatus("Sample capture opened."); } catch (error) { setError(error.message); setStatus("Sample did not load."); } event.target.value = ""; });
$("#showLabels").addEventListener("click", event => { showLabels = !showLabels; event.currentTarget.setAttribute("aria-pressed", String(showLabels)); drawAll(); });
$("#axesButton").addEventListener("click", () => setChartMode("axes"));
$("#magnitudeButton").addEventListener("click", () => setChartMode("magnitude"));
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

$("#verticalScale").addEventListener("change", event => { verticalScale = event.target.value; drawAll(); });
$("#fitVisibleButton").addEventListener("click", fitVisibleData);
$("#measureButton").addEventListener("click", () => { measuring = !measuring; drag = undefined; updateMeasurement(); drawAll(); });
$("#clearMeasurementButton").addEventListener("click", () => { cursors = [null, null]; updateMeasurement(); drawAll(); });
for (const [i, id] of ["#cursorAInput", "#cursorBInput"].entries()) {
  $(id).addEventListener("input", event => { if (event.target.value === "") { cursors[i] = null; updateMeasurement(false); drawAll(); } else setCursor(i, Number(event.target.value), false); });
  $(id).addEventListener("change", event => { if (event.target.value === "") { cursors[i] = null; updateMeasurement(); drawAll(); } else setCursor(i, Number(event.target.value)); });
}
