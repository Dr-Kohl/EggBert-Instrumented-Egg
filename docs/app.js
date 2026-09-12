"use strict";

const HEADER_BYTES = 32, MAGIC = [0x45, 0x47, 0x47, 0x31];
const FLAG_TRIGGERED = 1, FLAG_FREEFALL = 2, FLAG_FIFO_OVERRUN = 4;
let port, activeReader, capture, captureBytes;
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
  drawPlot($("#accelerationChart"), [capture.x, capture.y, capture.z], [-16, 16], ["#c33", "#17834d", "#2463c5"], "g");
  const maximumMagnitude = capture.magnitude.reduce((maximum, value) => Math.max(maximum, value), 2);
  drawPlot($("#magnitudeChart"), [capture.magnitude], [0, Math.ceil(maximumMagnitude)], ["#6240a0"], "g");
}

function drawPlot(canvas, series, domain, colors, unit) {
  const ctx = canvas.getContext("2d"), width = canvas.width, height = canvas.height, left = 58, right = 18, top = 18, bottom = 32;
  const plotWidth = width - left - right, plotHeight = height - top - bottom;
  ctx.clearRect(0, 0, width, height); ctx.font = "15px system-ui"; ctx.fillStyle = "#555"; ctx.strokeStyle = "#c8c8c8"; ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i += 1) { const y = top + plotHeight * i / 4, value = domain[1] - (domain[1] - domain[0]) * i / 4; ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(width - right, y); ctx.stroke(); ctx.fillText(`${value.toFixed(1)} ${unit}`, 3, y + 5); }
  const seconds = capture.sampleCount / capture.sampleRate;
  for (let i = 0; i <= 5; i += 1) { const x = left + plotWidth * i / 5; ctx.fillText(`${(seconds * i / 5).toFixed(1)} s`, x - 12, height - 8); }
  if (capture.triggerOffset !== 0xffffffff && capture.triggerOffset < capture.sampleCount) { const x = left + plotWidth * capture.triggerOffset / Math.max(1, capture.sampleCount - 1); ctx.strokeStyle = "#7a5f00"; ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(x, top); ctx.lineTo(x, height - bottom); ctx.stroke(); }
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
$("#saveButton").addEventListener("click", () => { const blob = new Blob([captureBytes], { type: "application/octet-stream" }), link = Object.assign(document.createElement("a"), { href: URL.createObjectURL(blob), download: "eggbert-capture.egg" }); link.click(); URL.revokeObjectURL(link.href); });
window.addEventListener("resize", drawAll);
