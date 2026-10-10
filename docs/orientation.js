/* Quaternion order throughout this module is [x, y, z, w]. */
(function (root) {
  "use strict";
  function normalize(q) {
    const length = Math.hypot(...q);
    if (!q.every(Number.isFinite) || length < .5 || length > 1.5) return null;
    return q.map(value => value / length);
  }
  function multiply(a, b) {
    const [x, y, z, w] = a, [X, Y, Z, W] = b;
    return [w*X+x*W+y*Z-z*Y, w*Y-x*Z+y*W+z*X, w*Z+x*Y-y*X+z*W, w*W-x*X-y*Y-z*Z];
  }
  function inverse(q) { return [-q[0], -q[1], -q[2], q[3]]; }
  function parse(line) {
    const match = /^Q([12]),(\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+)(?:,(-?\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+))?$/.exec(line);
    if (!match) return null;
    if ((match[1] === '2') !== (match[7] !== undefined)) return null;
    const q = normalize([+match[4], +match[5], +match[6], +match[3]].map(v => v / 1000000));
    if (!q) return null;
    const sample = { time: +match[2], q };
    if (match[1] === '2') {
      sample.accel = [+match[7], +match[8], +match[9]].map(v => v / 8192);
      sample.gyro = [+match[10], +match[11], +match[12]].map(v => v * .07);
      if ([...sample.accel, ...sample.gyro].some(v => !Number.isFinite(v))) return null;
    }
    return sample;
  }
  function rotate(q, v) { return multiply(multiply(q, [...v, 0]), inverse(q)).slice(0, 3); }
  const worldToView = [-Math.SQRT1_2, 0, 0, Math.SQRT1_2];
  // Physical measurements: screen-up is sensor +Z; buttons-down is sensor +Y.
  // CAD has +Y toward the buttons. CAD -> sensor is a half-turn about Z.
  const caseToSensor = [0, 0, 1, 0];
  const home = [1, 0, 0, 0];
  function absolute(q, heading = 0) {
    // SFLP maps body to a Z-up world. Three.js uses Y-up. Keep the existing
    // CAD home group, cancelling it here so it cannot reverse absolute tilt.
    const yaw = [0, Math.sin(heading / 2), 0, Math.cos(heading / 2)];
    return normalize(multiply(multiply(multiply(multiply(yaw, worldToView), q), caseToSensor), inverse(home)));
  }
  function heading(q) {
    const base = multiply(multiply(worldToView, q), caseToSensor), screen = rotate(base, [0, 0, 1]);
    // Face the screen toward the camera when it has a horizontal projection.
    // Face-up/down makes that projection undefined; use the USB axis instead.
    if (Math.hypot(screen[0], screen[2]) > .2) return Math.atan2(screen[2], screen[0]) + Math.PI / 2;
    const usb = rotate(base, [1, 0, 0]);
    return Math.atan2(usb[2], usb[0]);
  }
  function gravity(q) { return rotate(inverse(q), [0, 0, 1]); }
  function tilt(g) {
    const length = Math.hypot(...g);
    if (!Number.isFinite(length) || length < .1) return null;
    const [x,y,z] = g.map(v=>v/length);
    // Shortest rotation taking the measured upward direction to display +Y.
    const q = y < -.999999 ? [1,0,0,0] : [-z,0,x,1+y];
    const norm = Math.hypot(...q);
    return multiply(multiply(q.map(v=>v/norm),caseToSensor),inverse(home));
  }
  function pose(g) {
    const length = Math.hypot(...g);
    if (!Number.isFinite(length) || length < .1) return 'Unknown pose';
    const unit = g.map(v => v / length), axis = unit.reduce((best, v, i) => Math.abs(v) > Math.abs(unit[best]) ? i : best, 0);
    if (Math.abs(unit[axis]) < .9) return 'Tilted';
    return [['USB side up', 'USB side down'], ['Button side up', 'Button side down'], ['Screen down', 'Screen up']][axis][unit[axis] > 0 ? 1 : 0];
  }
  function stability() {
    let previous, since;
    return sample => {
      const dt = previous ? ((sample.time - previous.time) >>> 0) : 0;
      const g = sample.accel || gravity(sample.q), length = Math.hypot(...g);
      const dot = previous ? Math.abs(sample.q.reduce((sum, v, i) => sum + v * previous.q[i], 0)) : 1;
      const angularSpeed = dt ? 2 * Math.acos(Math.min(1, dot)) * 180 / Math.PI * 1000 / dt : 0;
      const gyroSpeed = sample.gyro ? Math.hypot(...sample.gyro) : angularSpeed;
      const change = previous ? Math.hypot(...g.map((v,i) => v - (previous.accel || gravity(previous.q))[i])) : 0;
      const predicted = gravity(sample.q), agreement = g.reduce((sum,v,i) => sum + v * predicted[i], 0) / length;
      const still = length >= .9 && length <= 1.1 && gyroSpeed < (sample.gyro ? 4 : 8) && angularSpeed < 12 && change < .04 && agreement > .985 && (!previous || dt <= 100);
      if (!still) since = undefined;
      else if (since === undefined) since = sample.time;
      previous = sample;
      return { ready: still && ((sample.time - since) >>> 0) >= 600, pose: pose(g), still };
    };
  }
  const api = { normalize, multiply, inverse, parse, rotate, absolute, heading, gravity, tilt, pose, stability };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.EggOrientation = api;
})(typeof globalThis !== "undefined" ? globalThis : this);
