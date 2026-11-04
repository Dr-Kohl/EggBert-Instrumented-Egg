"""Export the actual initial Home framebuffer as the 3D viewer's OLED texture.

Run tools/run_native_tests.py first, then this script. No duplicate menu/font
definitions: the texture follows apps.json and the production Home renderer.
"""
import struct
import zlib
from pathlib import Path

root=Path(__file__).resolve().parents[1]
frame=(root/'build/ui-preview.bin').read_bytes()[:1024]
if len(frame)!=1024:
    raise SystemExit('Generate UI previews before exporting the Home texture')
pixels=bytes(255 if frame[(x//8)*128+127-y]&(1<<(x%8)) else 0 for y in range(128) for x in range(64))


def chunk(kind,data):
    return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))


rows=b''.join(b'\0'+pixels[y*64:(y+1)*64] for y in range(128))
output=root/'docs/models/eggbert-home-screen.png'
output.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',64,128,8,0,0,0,0))+
                   chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
print(output)
