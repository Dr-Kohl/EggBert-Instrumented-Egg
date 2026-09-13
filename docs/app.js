"use strict";

const HEADER_BYTES = 32, MAGIC = [0x45, 0x47, 0x47, 0x31];
const FLAG_TRIGGERED = 1, FLAG_FREEFALL = 2, FLAG_FIFO_OVERRUN = 4;
let port, activeReader, capture, captureBytes, showLabels = true;
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

function displayCapture(bytes) {
  capture = parseEgg(bytes); captureBytes = bytes;
  $("#summary").hidden = false; $("#charts").hidden = false;
  $("#sampleCount").textContent = `${capture.sampleCount.toLocaleString()} samples`;
  $("#sampleRate").textContent = `${capture.sampleRate.toLocaleString()} Hz`;
  const trigger = (capture.flags & FLAG_TRIGGERED) ? ((capture.flags & FLAG_FREEFALL) ? "Freefall" : "Impact") : "No event";
  $("#triggerType").textContent = trigger + ((capture.flags & FLAG_FIFO_OVERRUN) ? " · FIFO overrun" : "");
  $("#integrity").textContent = capture.validCrc ? "CRC verified" : "CRC FAILED";
  $("#integrity").style.color = capture.validCrc ? "" : "#b00020";
  $("#saveButton").disabled = false; drawAll();
}

function drawAll() {
  if (!capture) return;
  const events = detectEvents();
  drawPlot($("#accelerationChart"), [capture.x, capture.y, capture.z], [-16, 16], ["#c33", "#17834d", "#2463c5"], "g", events);
  const maximumMagnitude = capture.magnitude.reduce((maximum, value) => Math.max(maximum, value), 2);
  drawPlot($("#magnitudeChart"), [capture.magnitude], [0, Math.ceil(maximumMagnitude)], ["#6240a0"], "g", events);
}

function detectEvents() {
  const events = [];
  const add = (index, label, color) => { if (index >= 0 && index < capture.sampleCount && !events.some(event => Math.abs(event.index - index) < Math.max(1, capture.sampleRate * 0.04))) events.push({ index, label, color }); };
  if (capture.triggerOffset !== 0xffffffff && capture.triggerOffset < capture.sampleCount) add(capture.triggerOffset, "Trigger", "#7a5f00");
  let peakIndex = 0, peak = 0;
  for (let i = 0; i < capture.sampleCount; i += 1) {
    if (capture.magnitude[i] > peak) { peak = capture.magnitude[i]; peakIndex = i; }
    if (Math.abs(capture.x[i]) >= 15.9 || Math.abs(capture.y[i]) >= 15.9 || Math.abs(capture.z[i]) >= 15.9) add(i, "Saturation", "#b00020");
  }
  const freefallIndex = capture.magnitude.findIndex(value => value < 0.25);
  if (freefallIndex >= 0) add(freefallIndex, "Freefall", "#2463c5");
  if (peak > 2) add(peakIndex, "Peak impact", "#b00020");
  if (events.length === 0 || peak <= 2) add(0, "Gravity baseline", "#6240a0");
  return events.sort((a, b) => a.index - b.index);
}

function drawPlot(canvas, series, domain, colors, unit, events) {
  const ctx = canvas.getContext("2d"), width = canvas.width, height = canvas.height, left = 58, right = 18, top = 18, bottom = 32;
  const plotWidth = width - left - right, plotHeight = height - top - bottom;
  ctx.clearRect(0, 0, width, height); ctx.font = "15px system-ui"; ctx.fillStyle = "#555"; ctx.strokeStyle = "#c8c8c8"; ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i += 1) { const y = top + plotHeight * i / 4, value = domain[1] - (domain[1] - domain[0]) * i / 4; ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(width - right, y); ctx.stroke(); ctx.fillText(`${value.toFixed(1)} ${unit}`, 3, y + 5); }
  const seconds = capture.sampleCount / capture.sampleRate;
  for (let i = 0; i <= 5; i += 1) { const x = left + plotWidth * i / 5; ctx.fillText(`${(seconds * i / 5).toFixed(1)} s`, x - 12, height - 8); }
  if (showLabels) events.forEach(event => { const x = left + plotWidth * event.index / Math.max(1, capture.sampleCount - 1); ctx.strokeStyle = event.color; ctx.lineWidth = 1.5; ctx.setLineDash([5, 4]); ctx.beginPath(); ctx.moveTo(x, top); ctx.lineTo(x, height - bottom); ctx.stroke(); ctx.setLineDash([]); ctx.save(); ctx.font = "bold 13px system-ui"; const labelWidth = ctx.measureText(event.label).width; const labelX = Math.min(width - 8, Math.max(left + 8, x)); ctx.translate(labelX, top + labelWidth + 6); ctx.rotate(-Math.PI / 2); ctx.fillStyle = "rgba(255,255,255,0.86)"; ctx.fillRect(-3, -15, labelWidth + 6, 17); ctx.fillStyle = event.color; ctx.fillText(event.label, 0, 0); ctx.restore(); });
  series.forEach((values, index) => { ctx.strokeStyle = colors[index]; ctx.lineWidth = 1.15; ctx.beginPath(); const stride = Math.max(1, Math.ceil(values.length / plotWidth)); for (let i = 0; i < values.length; i += stride) { const x = left + plotWidth * i / Math.max(1, values.length - 1), y = top + plotHeight * (domain[1] - values[i]) / (domain[1] - domain[0]); if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y); } ctx.stroke(); });
}

async function connect() {
  if (!("serial" in navigator)) throw new Error("Web Serial is unavailable. Use Chrome or Edge over HTTPS, or open a saved .egg file.");
  port = await navigator.serial.requestPort(); await port.open({ baudRate: 115200, bufferSize: 65536 });
  $("#connectButton").textContent = "EggBert connected"; $("#connectButton").disabled = true; $("#armButton").disabled = false; $("#disconnectButton").disabled = false; $("#downloadButton").disabled = false; setStatus("EggBert connected. Arm it, then perform the test.");
}

async function disconnect() {
  if (activeReader) await activeReader.cancel();
  if (port) await port.close();
  port = undefined; activeReader = undefined;
  $("#connectButton").textContent = "Connect EggBert"; $("#connectButton").disabled = false; $("#armButton").disabled = true; $("#disconnectButton").disabled = true; $("#downloadButton").disabled = true;
  setStatus("Device disconnected.");
}

async function downloadCapture() {
  if (!port) return; setError(""); setStatus("Requesting capture…"); $("#downloadButton").disabled = true;
  const writer = port.writable.getWriter(); await writer.write(new TextEncoder().encode("download\n")); writer.releaseLock();
  const reader = port.readable.getReader(); activeReader = reader; let received = new Uint8Array(), required = null, start = -1;
  try {
    while (required === null || received.length - start < required) {
      const result = await Promise.race([reader.read(), new Promise(resolve => setTimeout(() => resolve({ timeout: true }), 10000))]);
      if (result.timeout) { await reader.cancel(); throw new Error("EggBert did not start a binary download. Confirm CAPTURE COMPLETE, then try again."); }
      const { value, done } = result; if (done) throw new Error("EggBert disconnected during transfer."); received = concat(received, value);
      if (start < 0) start = findMagic(received);
      if (start < 0) { const text = new TextDecoder().decode(received); if (text.includes("ERROR:")) throw new Error(text.trim()); }
      if (start >= 0 && received.length >= start + HEADER_BYTES) { const view = new DataView(received.buffer, received.byteOffset + start, HEADER_BYTES); required = view.getUint16(6, true) + view.getUint32(12, true) * view.getUint16(26, true); setStatus(`Downloading ${required.toLocaleString()} bytes…`); }
    }
    displayCapture(received.slice(start, start + required)); setStatus("Capture downloaded and verified.");
  } finally { if (activeReader === reader) activeReader = undefined; reader.releaseLock(); $("#downloadButton").disabled = false; }
}

async function armEggBert() {
  if (!port) return;
  const writer = port.writable.getWriter();
  await writer.write(new TextEncoder().encode("arm\n"));
  writer.releaseLock();
  setError(""); setStatus("EggBert is arming. Hold it still until its screen says WAIT EVENT.");
}

$("#connectButton").addEventListener("click", () => connect().catch(error => { setError(error.message); setStatus("No device connected."); }));
$("#armButton").addEventListener("click", () => armEggBert().catch(error => setError(error.message)));
$("#disconnectButton").addEventListener("click", () => disconnect().catch(error => setError(error.message)));
$("#downloadButton").addEventListener("click", () => downloadCapture().catch(error => { setError(error.message); setStatus("Download did not complete."); }));
$("#fileInput").addEventListener("change", async event => { try { setError(""); displayCapture(new Uint8Array(await event.target.files[0].arrayBuffer())); setStatus("Capture file opened."); } catch (error) { setError(error.message); } event.target.value = ""; });
$("#sampleSelect").addEventListener("change", async event => { const filename = event.target.value; if (!filename) return; try { setError(""); setStatus("Loading sample capture…"); const response = await fetch("sample-data/" + filename); if (!response.ok) throw new Error("Could not load sample capture (" + response.status + ")."); displayCapture(new Uint8Array(await response.arrayBuffer())); setStatus("Sample capture opened."); } catch (error) { setError(error.message); setStatus("Sample did not load."); } event.target.value = ""; });
$("#showLabels").addEventListener("change", event => { showLabels = event.target.checked; drawAll(); });
$("#saveButton").addEventListener("click", () => { const blob = new Blob([captureBytes], { type: "application/octet-stream" }), link = Object.assign(document.createElement("a"), { href: URL.createObjectURL(blob), download: "eggbert-capture.egg" }); link.click(); URL.revokeObjectURL(link.href); });
window.addEventListener("resize", drawAll);
