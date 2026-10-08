#!/usr/bin/env python3
"""Keep one USB connection for real display/network lifecycle and soak checks."""
import argparse,json,re,time,zlib
from pathlib import Path
import serial
from PIL import Image

args=argparse.ArgumentParser();args.add_argument('--port',default='/dev/cu.usbmodem11201')
args.add_argument('--seconds',type=int,default=1800);opt=args.parse_args()
out=Path('backups/device_validation');out.mkdir(parents=True,exist_ok=True);out.chmod(0o700)
port=serial.Serial(port=None,baudrate=115200,timeout=.15);port.dtr=False;port.rts=False;port.port=opt.port;port.open()
log=open(out/'runtime.log','wb',buffering=0);(out/'runtime.log').chmod(0o600)
samples=[];failures=[]

def read_for(seconds):
    data=bytearray();end=time.monotonic()+seconds
    while time.monotonic()<end:
        chunk=port.read(4096);data.extend(chunk);log.write(chunk)
    return bytes(data)

def command(text,delay=1):
    port.write((text+'\n').encode());return read_for(delay)

def status():
    data=command('status',2).decode('utf8','replace')
    lines=re.findall(r'SH_STATUS ([^\r\n]+)',data)
    if not lines:raise RuntimeError('Device status unavailable')
    values={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',lines[-1])}
    samples.append(values);return values

def await_state(key,value,timeout=25):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        state=status()
        if state.get(key)==value:return state
        read_for(.5)
    raise AssertionError(f'{key} did not become {value}')

def capture(name,expected_page):
    port.write(b'snapshot\n');data=bytearray();end=time.monotonic()+20
    while time.monotonic()<end:
        data.extend(port.read(4096))
        match=re.search(rb'SH_FRAME (\d+) (\d+) (\d+) (\d+) (\d+) ([0-9a-f]+)\r?\n',data)
        if match:
            count,w,h,stride,page=map(int,match.groups()[:5]);crc=int(match.group(6),16)
            if len(data)>=match.end()+count:
                log.write(data[:match.end()]);log.write(data[match.end()+count:])
                assert w==h==360 and stride>=720 and count>=stride*h
                assert page==expected_page,f'Expected page {expected_page}, device captured {page}'
                raw=data[match.end():match.end()+count];assert zlib.crc32(raw)&0xffffffff==crc,'Framebuffer checksum mismatch';rgb=bytearray()
                for y in range(h):
                    for x in range(w):
                        pos=y*stride+x*2;v=raw[pos]|raw[pos+1]<<8
                        rgb.extend((round(((v>>11)&31)*255/31),round(((v>>5)&63)*255/63),round((v&31)*255/31)))
                Image.frombytes('RGB',(w,h),bytes(rgb)).save(out/(name+'.png'))
                (out/(name+'.png')).chmod(0o600);return
    log.write(data if not match else data[:match.end()])
    raise TimeoutError(f'Framebuffer capture timed out ({len(data)} bytes, header={bool(match)})')

try:
    read_for(20)
    startup_resets=(out/'runtime.log').read_bytes().count(b'rst:')
    state=await_state('connected',1)
    await_state('weather',1);await_state('clock',1)
    print('DEVICE: Wi-Fi, actual weather and SNTP ready',flush=True)
    for p,name in list(enumerate(['home','weather','quota','detail','standby']))*3:
        reply=command(f'page {p}',3);assert f'SH_PAGE {p}'.encode() in reply
        capture(name,p)
        print('CAPTURE:',name,flush=True)
    reply=command('page 2',3);assert b'SH_PAGE 2' in reply
    for amount,name in [(0,'quota_zero'),(500,'quota_full'),(340,None)]:
        reply=command(f'quota 0 {amount} 500 1',3)
        assert f'SH_QUOTA 0 {amount} 500 1'.encode() in reply
        if name:capture(name,2)
    for i in range(3):
        command('pair');await_state('pairing',1)
        command('cancel');await_state('pairing',0)
        state=status();assert state['saved']==1
        command('connect');await_state('connected',1);await_state('weather',1)
        print(f'BLE same-boot restart/cancel/reconnect cycle {i+1}: PASS',flush=True)
    command('refresh',.1)
    command('disconnect');state=await_state('connected',0);assert state['saved']==1
    read_for(5);state=status();assert state['connected']==0 and state['stale']==1
    command('connect');await_state('connected',1);await_state('weather',1)
    started=time.monotonic();next_refresh=started
    iteration=0
    while time.monotonic()-started<opt.seconds:
        command(f'page {iteration%5}');iteration+=1
        state=status();assert state['saved']==1 and state['connected']==1
        if time.monotonic()>=next_refresh:
            command('refresh');next_refresh=time.monotonic()+60
        read_for(8)
        if iteration%5==0:print(f'SOAK {int(time.monotonic()-started)}s internal={state["internal"]} largest={state["largest"]} psram={state["psram"]}',flush=True)
    command('page 0');capture('home_after_soak',0)
    runtime=(out/'runtime.log').read_bytes()
    assert not re.search(rb'Guru Meditation|stack overflow|panic\b|assert failed|\*\*\*ERROR\*\*\*',runtime,re.I),'Firmware reported a fatal error'
    assert runtime.count(b'rst:')==startup_resets,'Device reset during validation'
    assert min(s['console_stack'] for s in samples)>512,'Diagnostic stack headroom too small'
    result={'soak_seconds':int(time.monotonic()-started),'iterations':iteration,'samples':samples,'failures':failures,
        'flashed_app':json.loads(Path('firmware/manifest.json').read_text())['files']['shanhai_echoear_demo.bin'],
        'checks':['live-weather','SNTP','15-framebuffers-with-page-and-CRC','quota-zero-full','3-BLE-restart-cancel-cycles','disconnect-inhibits-reconnect','inflight-weather-disconnect-retains-stale','saved-network-reconnect','no-fatal-or-unexpected-reset','diagnostic-stack-headroom']}
    (out/'results.json').write_text(json.dumps(result,indent=2)+'\n');print('DEVICE VALIDATION PASS',flush=True)
except BaseException as error:
    failures.append(str(error));(out/'results.json').write_text(json.dumps({'samples':samples,'failures':failures},indent=2)+'\n')
    raise
finally:
    log.close();port.close()
