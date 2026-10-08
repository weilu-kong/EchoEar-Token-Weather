#!/usr/bin/env python3
"""Generate native LVGL assets/layout/font C from the approved UI source."""
import json
import os
import re
import subprocess
from pathlib import Path
from PIL import Image
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

ROOT=Path(__file__).resolve().parents[1]
MAIN=ROOT/'main'
OUT=ROOT/'assets/firmware'
FONTS=MAIN/'fonts'
OUT.mkdir(exist_ok=True);FONTS.mkdir(exist_ok=True)
layout=json.loads((ROOT/'tools/layout.json').read_text())
node=os.environ.get('NODE_BIN','/Users/kongweilu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node')
converter=os.environ.get('LV_FONT_CONV','/Users/kongweilu/.npm/_npx/b62fd1a864044392/node_modules/lv_font_conv/lv_font_conv.js')
env={**os.environ,'SHARP_MODULE':os.environ.get('SHARP_MODULE','/Users/kongweilu/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/sharp')}
subprocess.run([node,str(ROOT/'tools/rasterize_icons.cjs')],env=env,check=True)

headers=['// Generated from approved tools/layout.json.','#pragma once','#include "lvgl.h"','#include <stdint.h>',
 'typedef enum {SH_TEXT,SH_PANEL,SH_ICON,SH_LOGO,SH_RING,SH_LINE} sh_kind_t;',
 'typedef struct {uint8_t kind;int16_t x,y,w,h,size,weight,provider,radius,stroke;uint32_t color;uint8_t alpha;const char *text,*key,*icon;} sh_node_t;',
 'typedef struct {int16_t x,y,w,h;const char *action;} sh_hit_t;']
kind={'text':'SH_TEXT','panel':'SH_PANEL','icon':'SH_ICON','logo':'SH_LOGO','ring':'SH_RING','line':'SH_LINE'}
for i,p in enumerate(layout['pages']):
 rows=[]
 for n in p['nodes']:
  fields=[kind[n['type']],n['x'],n['y'],n.get('w',0),n.get('h',0),round(n.get('size',14)),n.get('weight',500),-1 if n.get('provider')=='selected' else n.get('provider',-1),n.get('r',0),n.get('width',0),int(n.get('color','#fffaf0').lstrip('#'),16),round(n.get('alpha',1)*255),json.dumps(n.get('text',''),ensure_ascii=False),json.dumps(n.get('key','')),json.dumps(n.get('icon',''))]
  rows.append('{'+','.join(str(v) for v in fields)+'}')
 headers.append(f'static const sh_node_t sh_page_{i}[]={{'+','.join(rows)+'};')
 hits=p['hits'] or [{'x':0,'y':0,'w':0,'h':0,'action':''}]
 headers.append(f'static const sh_hit_t sh_hits_{i}[]={{'+','.join('{'+','.join(str(h[k]) for k in ('x','y','w','h'))+','+json.dumps(h['action'])+'}' for h in hits)+'};')
headers+=['static const sh_node_t *const sh_pages[]={'+','.join('sh_page_'+str(i) for i in range(len(layout['pages'])))+'};',
 'static const uint8_t sh_page_counts[]={'+','.join(str(len(p['nodes'])) for p in layout['pages'])+'};',
 'static const sh_hit_t *const sh_page_hits[]={'+','.join('sh_hits_'+str(i) for i in range(len(layout['pages'])))+'};',
 'static const uint8_t sh_hit_counts[]={'+','.join(str(len(p['hits'])) for p in layout['pages'])+'};']
(MAIN/'sh_scene_data.h').write_text('\n'.join(headers)+'\n')

c=['// Generated native images.','#include "sh_assets.h"']
h=['#pragma once','#include "lvgl.h"','const lv_font_t *sh_font(int size,int weight);','const lv_image_dsc_t *sh_icon(const char *name);']
background_names=list(dict.fromkeys(p['background'] for p in layout['pages']))
for name in background_names:
 raw=(ROOT/'assets/backgrounds'/f'{name}.rgb565').read_bytes()
 symbol=f'sh_bg_{name}'
 c.append(f'extern const uint8_t {symbol}_raw[] asm("_binary_{name}_rgb565_start");')
 c.append(f'const lv_image_dsc_t {symbol}={{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=360,.h=360,.stride=720}},.data_size={len(raw)},.data={symbol}_raw}};')
 h.append(f'extern const lv_image_dsc_t {symbol};')
icon_names=[]
for path in sorted((OUT/'icons').glob('*.png')):
 image=Image.open(path).convert('RGBA');name=path.stem;icon_names.append(name)
 if name not in ('codex','claude','cursor','antigravity'):
  data=image.getchannel('A').tobytes();cf='LV_COLOR_FORMAT_A8';stride=image.width
 else:
  data=image.tobytes('raw','BGRA');cf='LV_COLOR_FORMAT_ARGB8888';stride=image.width*4
 symbol='sh_img_'+name
 c.append(f'static const uint8_t {symbol}_raw[]={{'+','.join(str(v) for v in data)+'};')
 c.append(f'static const lv_image_dsc_t {symbol}={{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf={cf},.w={image.width},.h={image.height},.stride={stride}}},.data_size={len(data)},.data={symbol}_raw}};')
c.append('#include <string.h>\nconst lv_image_dsc_t *sh_icon(const char *name){')
for name in icon_names:c.append(f'if(!strcmp(name,"{name}"))return &sh_img_{name};')
c.append('return &sh_img_unknown;}')

pairs={(round(n.get('size',14)),n.get('weight',500)) for p in layout['pages'] for n in p['nodes'] if n['type']=='text'}
pairs.update({(14,500),(10,500),(11,500),(12,500),(13,600),(15,600),(17,600),(18,600),(19,600)})
ui=(MAIN/'sh_ui.c').read_text() if (MAIN/'sh_ui.c').exists() else ''
alltext=json.dumps(layout,ensure_ascii=False)+(ROOT/'tools/preview.js').read_text()+ui+'年月日月火水木金土時刻未同期接続設定読み込み中操作できません設定失敗'
symbols=''.join(sorted(set(re.findall(r'[^\x00-\x7f]',alltext))))
symbols+=''.join(chr(i) for i in range(32,127))+'°↑↓'
for weight in sorted({w for s,w in pairs}):
 ttf=OUT/f'NotoSansJP-{weight}.ttf'
 if not ttf.exists():instantiateVariableFont(TTFont(ROOT/'assets/fonts/NotoSansJP.ttf'),{'wght':weight},inplace=True).save(ttf)
for size,weight in sorted(pairs):
 name=f'sh_font_{size}_{weight}';dst=FONTS/f'{name}.c'
 font_symbols=symbols if size<25 else '0123456789:%°C /.-kM∞'
 if (size,weight)==(14,500):font_symbols=None
 source=OUT/f'NotoSansJP-{weight}.ttf'
 if font_symbols is None:
  source=OUT/'NotoSansCJKjp-500.ttf'
  if not source.exists():instantiateVariableFont(TTFont(ROOT/'assets/fonts/NotoSansCJKjp.ttf'),{'wght':500},inplace=True).save(source)
 subprocess.run([node,converter,'--font',str(source),*(['--symbols',font_symbols] if font_symbols else ['--range','0x20-0x024F,0x2000-0x206F,0x3000-0x30FF,0x4E00-0x9FFF,0xFF00-0xFFEF']),'--size',str(size),'--bpp','4','--format','lvgl','--lv-include','lvgl.h','--no-compress','--lv-font-name',name,'-o',str(dst)],check=True,stdout=subprocess.DEVNULL)
 h.append(f'extern const lv_font_t {name};')
c.append('const lv_font_t *sh_font(int size,int weight){')
for size,weight in sorted(pairs):c.append(f'if(size=={size}&&weight=={weight})return &sh_font_{size}_{weight};')
c.append('return &sh_font_12_500;}')
(MAIN/'sh_assets.h').write_text('\n'.join(h)+'\n')
(MAIN/'sh_assets.c').write_text('\n'.join(c)+'\n')
print(f'Generated {len(background_names)} backgrounds, {len(icon_names)} images, {len(pairs)} fonts, {len(layout['pages'])} native scene layouts.')
