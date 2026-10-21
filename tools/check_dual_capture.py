"""Validate engineering EGD2 raw capture and optional existing EGG1 export."""
import argparse
import json
import math
from pathlib import Path
import statistics
import struct
import zlib


def check_capture(path, legacy=None):
    frame = Path(path).read_bytes()
    if len(frame) < 32:
        raise ValueError('Truncated EGD2 header')
    magic, version, flags, header, rate, count, low_ug, high_ug, stride, crc = struct.unpack_from('<4sBBH6I',frame)
    if (magic,version,flags,header,rate,count,low_ug,high_ug,stride) != (b'EGD2',1,0,32,3840,19200,488,10417,12):
        raise ValueError('Unexpected EGD2 metadata or capture integrity failure')
    payload = frame[header:]
    if len(payload) != count*stride or zlib.crc32(payload) != crc:
        raise ValueError('EGD2 length/CRC mismatch')
    rows = list(struct.iter_unpack('<6h',payload))
    report = {'samples_per_stream':count,'nominal_rate_hz':rate,'nominal_duration_seconds':count/rate,
              'payload_bytes':len(payload),'crc32':f'{crc:08x}','streams':{}}
    for name,begin,scale in [('low',0,low_ug/1e6),('high',3,high_ug/1e6)]:
        axes = [[r[begin+a]*scale for r in rows] for a in range(3)]
        means = [statistics.mean(a) for a in axes]
        report['streams'][name] = {'mean_xyz_g':means,'mean_vector_magnitude_g':math.sqrt(sum(v*v for v in means)),
                                  'stddev_xyz_g':[statistics.pstdev(a) for a in axes],
                                  'peak_axis_g':max(abs(v) for a in axes for v in a)}
    if legacy:
        old=Path(legacy).read_bytes()
        if old[:4] not in (b'EGG1',b'EGG2') or len(old)<32: raise ValueError('Invalid .egg export')
        old_header=struct.unpack_from('<H',old,6)[0]
        old_rate,old_count=struct.unpack_from('<II',old,8)
        old_scale,old_stride,old_crc=struct.unpack_from('<HHI',old,24)
        old_payload=old[old_header:]
        dual=old[:4]==b'EGG2'
        if (old_rate,old_count,old_scale,old_stride)!=(rate,count,2048,12 if dual else 6): raise ValueError('.egg metadata mismatch')
        if len(old_payload)!=count*old_stride or zlib.crc32(old_payload)!=old_crc: raise ValueError('.egg CRC/length mismatch')
        if dual:
            if old_header!=80 or old[4]!=2 or zlib.crc32(old[:76])!=struct.unpack_from('<I',old,76)[0]: raise ValueError('EGG2 header CRC/metadata mismatch')
            if old_payload!=payload: raise ValueError('EGG2 did not preserve both raw streams')
            report['egg2_raw_streams_preserved']=True
        report['legacy_crc32']=f'{old_crc:08x}'
        report['legacy_trigger_offset']=struct.unpack_from('<I',old,16)[0]
        # A calibrated legacy export may differ from raw low-g; report, do not assume calibration is identity.
        if not dual:
            legacy_rows=list(struct.iter_unpack('<3h',old_payload))
            report['legacy_matches_raw_low']=all(tuple(r[:3])==v for r,v in zip(rows,legacy_rows))
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    parser.add_argument('--legacy','--egg',dest='legacy',type=Path)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    report=json.dumps(check_capture(args.capture,args.legacy),indent=2)
    if args.report: args.report.write_text(report+'\n',encoding='utf8')
    print(report)
