// Shared whole-record Hammer Hit analysis. No smoothing or threshold interpolation.
(function(root) {
  "use strict";
  function analyze(record) {
    let peakMg=0, count=0, start=-1;
    const intervals=[];
    const magnitude=record.dual ? new Float64Array(record.sampleCount) : record.auto.magnitude;
    let clipped=false;
    for (let i=0;i<record.sampleCount;i++) {
      peakMg=Math.max(peakMg,Math.round(record.auto.magnitude[i]*1000));
      let above=false;
      if (record.dual) {
        const x=record.rawHigh[3*i], y=record.rawHigh[3*i+1], z=record.rawHigh[3*i+2];
        const squared=x*x+y*y+z*z;
        magnitude[i]=Math.sqrt(squared)*0.010417;
        above=squared*10417*10417>20000000*20000000;
        clipped ||= Math.max(Math.abs(x),Math.abs(y),Math.abs(z))>=30413;
      }
      if (above) { count++; if(start<0) start=i; }
      else if(start>=0) { intervals.push([start,i]); start=-1; }
    }
    if(start>=0) intervals.push([start,record.sampleCount]);
    const invalid=!record.validCrc || !!(record.flags & (4|32)) || !(record.flags & 1);
    const unavailable=!record.dual || clipped || invalid || !!(record.flags & 64);
    const reason=!record.dual ? "Time above 20 g requires a V2 recording with the high-g sensor." :
      invalid ? "Recording integrity or trigger error: results are unreliable." :
      clipped || (record.flags & 64) ? "High-g sensor clipped: time above 20 g is unavailable." : "";
    return {peakG:Math.floor(peakMg/100)/10,count, milliseconds:unavailable ? null : count*1000/record.sampleRate,
      intervals,magnitude,reason,invalid};
  }
  root.EggHammer={analyze};
  if(typeof module!=="undefined") module.exports=root.EggHammer;
})(globalThis);
