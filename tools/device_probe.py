#!/usr/bin/env python3
"""Read diagnostic state or capture the real LVGL framebuffer over USB."""
import argparse
import re
import time
import zlib
from pathlib import Path
import serial
from PIL import Image

p=argparse.ArgumentParser();p.add_argument('--port',default='/dev/cu.usbmodem11201')
p.add_argument('--command',default='status');p.add_argument('--capture',type=Path)
args=p.parse_args()
port=serial.Serial(port=None,baudrate=115200,timeout=.2)
port.dtr=False;port.rts=False;port.port=args.port;port.open()
port.reset_input_buffer();port.write((('snapshot' if args.capture else args.command)+'\n').encode())
data=bytearray();end=time.monotonic()+(20 if args.capture else 3)
try:
    while time.monotonic()<end:
        data.extend(port.read(4096))
        if args.capture:
            match=re.search(rb'SH_FRAME (\d+) (\d+) (\d+) (\d+) (\d+) ([0-9a-f]+)\r?\n',data)
            if match:
                count,w,h,stride,page=map(int,match.groups()[:5]);crc=int(match.group(6),16)
                if len(data)>=match.end()+count:
                    assert w==h==360 and stride>=720 and count>=stride*h
                    raw=data[match.end():match.end()+count];assert zlib.crc32(raw)&0xffffffff==crc,'Framebuffer checksum mismatch';rgb=bytearray()
                    for y in range(h):
                        for x in range(w):
                            offset=y*stride+x*2;v=raw[offset]|raw[offset+1]<<8
                            rgb.extend((round(((v>>11)&31)*255/31),round(((v>>5)&63)*255/63),round((v&31)*255/31)))
                    args.capture.parent.mkdir(parents=True,exist_ok=True)
                    Image.frombytes('RGB',(w,h),bytes(rgb)).save(args.capture)
                    args.capture.chmod(0o600)
                    print(f'Captured actual LVGL framebuffer: page={page}, {w}x{h}, {count} bytes -> {args.capture}')
                    break
    else:
        if args.capture:raise TimeoutError('No complete framebuffer received')
    if not args.capture:
        for line in data.decode('utf8','replace').splitlines():
            line=re.sub(r'\x1b\[[0-9;]*m','',line)
            if re.search(r'password|\bpop\b|shared key|verifier|device random',line,re.I):continue
            if 'SH_STATUS' in line or 'SH_COMMAND' in line or 'sh_network:' in line or 'sh_ui:' in line or 'panic' in line or 'assert' in line:print(line)
finally:port.close()
