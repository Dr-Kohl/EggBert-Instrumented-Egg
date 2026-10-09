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
    const match = /^Q1,(\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+)$/.exec(line);
    if (!match) return null;
    const q = normalize([+match[3], +match[4], +match[5], +match[2]].map(v => v / 1000000));
    return q ? { time: +match[1], q } : null;
  }
  function relative(reference, current) {
    // Map sensor axes through the existing X=pi home transform: +Y down,
    // +Z toward the camera, +X USB to the viewer's left.
    const home = [1, 0, 0, 0];
    return normalize(multiply(multiply(home, multiply(inverse(reference), current)), inverse(home)));
  }
  const api = { normalize, multiply, inverse, parse, relative };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.EggOrientation = api;
})(typeof globalThis !== "undefined" ? globalThis : this);
