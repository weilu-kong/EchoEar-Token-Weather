'use strict';
const canvas = document.getElementById('lcd');
const ctx = canvas.getContext('2d');
const pageIds = ['home','weather','quota','detail','calendar','event','standby'];
const images = {};
let providers = structuredClone(SCENE.providers);
let weather = structuredClone(SCENE.weather);
let calendar = structuredClone(SCENE.calendar), selectedEvent = 0, descriptionOffset = 0, calendarOffset = 0;
let network = { saved: true, connected: true, autoReconnect: true, ssid: 'EchoEar-Demo' };
let currentPage = 'home', selectedProvider = 2, overlay = null, clock = '10:28';
let lastActivity = performance.now(), pointerStart = null;
let idleEnabled = true;
window.previewReady = false;
const calendarLineHeight=SCENE.calendar_line_height, calendarBaseline=SCENE.calendar_font_baseline;
function calendarCount(){return calendar.valid?Math.min(calendar.events.length,12):0;}
function clampCalendar(){const count=calendarCount();calendarOffset=Math.min(calendarOffset,count?Math.floor((count-1)/2)*2:0);if(selectedEvent>=count)selectedEvent=calendarOffset;}
function changeCalendarPage(delta){
  const count=calendarCount(),last=count?Math.floor((count-1)/2)*2:0;
  calendarOffset=Math.max(0,Math.min(last,calendarOffset+delta*2));selectedEvent=calendarOffset;activity();syncControls();draw();
}

const iconPaths = {
  wifi: '<path d="M3 8a14 14 0 0 1 18 0M6 12a9 9 0 0 1 12 0m-9 4a4 4 0 0 1 6 0"/><circle cx="12" cy="20" r="1" fill="currentColor"/>',
  pin: '<path d="M18 9c0 5-6 12-6 12S6 14 6 9a6 6 0 1 1 12 0Z"/><circle cx="12" cy="9" r="2"/>',
  sun: '<circle cx="12" cy="12" r="4" fill="currentColor" stroke="none"/><path d="M12 1v3m0 16v3M1 12h3m16 0h3M4 4l2 2m12 12 2 2M4 20l2-2M18 6l2-2"/>',
  moon: '<path d="M19 16A9 9 0 0 1 8 3a9 9 0 1 0 11 13Z" fill="currentColor" stroke="none"/>',
  cloud: '<path d="M6 18a5 5 0 0 1-1-10 7 7 0 0 1 13-1 5.5 5.5 0 0 1 0 11Z" fill="currentColor" stroke="none"/>',
  rain: '<path d="M5 14a4 4 0 0 1 0-8 6 6 0 0 1 11-1 4.5 4.5 0 0 1 2 9Z" fill="currentColor" stroke="none"/><path d="m7 17-1 3m6-3-1 3m6-3-1 3"/>',
  drop: '<path d="M12 2S5 10 5 15a7 7 0 0 0 14 0C19 10 12 2 12 2Z"/><path d="M8 15a4 4 0 0 0 4 4"/>',
  thermometer: '<path d="M9 15V5a3 3 0 0 1 6 0v10a5 5 0 1 1-6 0Z"/><path d="M12 8v10"/>',
  range: '<path d="M7 20V4m-4 4 4-4 4 4m6-4v16m-4-4 4 4 4-4"/>',
  clock: '<circle cx="12" cy="12" r="9"/><path d="M12 6v6l4 2"/>',
  calendar: '<rect x="3" y="5" width="18" height="16" rx="2"/><path d="M7 2v6m10-6v6M3 11h18m-13 4h2m4 0h2"/>',
  spark: '<path d="m12 1 3 8 8 3-8 3-3 8-3-8-8-3 8-3Z"/>',
  back: '<path d="m15 4-8 8 8 8"/>',
  warning: '<path d="M12 2 1 21h22Z"/><path d="M12 8v6m0 3v1"/>',
  snow: '<path d="M12 1v22M2 6l20 12M2 18 22 6m-13-3 3 3 3-3m-6 18 3-3 3 3"/>',
  fog: '<path d="M3 7h18M1 12h22M3 17h18"/>',
  unknown: '<circle cx="12" cy="12" r="8"/><path d="M8 12h8"/>',
};
iconPaths.wifi_off = iconPaths.wifi + '<path d="M2 2 22 22"/>';
function activity() { lastActivity = performance.now(); }
function quotaPercent(p) {
  if (!p.valid) return null;
  if (p.unlimited) return null;
  if (Number.isFinite(p.percent)) return Math.max(0,Math.min(100,Math.round(p.percent)));
  return p.has_total && p.total > 0 ? Math.max(0, Math.min(100, Math.round(100 * p.remaining / p.total))) : null;
}
function amount(n) {
  if (n >= 1000000) return `${+(n / 1000000).toFixed(1)}M`;
  if (n >= 10000) return `${+(n / 1000).toFixed(1)}k`;
  return n.toLocaleString('en-US');
}
const stateLabels={ready:'接続済み',loading:'取得中',stale:'前回データ',auth:'認証が必要',rate:'取得制限中',error:'取得失敗・未対応',unconfigured:'未接続'};
function balance(p) {
  if(!p.valid)return stateLabels[p.state] || '未接続';
  if(p.unlimited)return '無制限';
  if(p.cursorBuckets)return Number.isFinite(p.percent)?`残り ${quotaPercent(p)}%`:'Cursor Models 未提供';
  if(Number.isFinite(p.percent))return `残り ${quotaPercent(p)}%`;
  if(p.percentOnly)return '残量未提供';
  return p.has_total ? `${amount(p.remaining)} / ${amount(p.total)}${p.demo?'':` ${p.unit || ''}`}` : `${amount(p.remaining)} ${p.unit || ''}`;
}
function quotaWindow(p){
  const minutes=p.window_minutes || 0;
  const primary=p.window_label || (minutes===0?'利用窓未提供':minutes%1440===0?`${minutes/1440}日`:minutes%60===0?`${minutes/60}時間`:`${minutes}分`);
  return `${p.unit || ''} ${primary}`;
}
function condition() {
  const c = weather.code;
  if (weather.status === 'empty') return '天気を取得中';
  if (c === 0) return '晴れ';
  if ([1, 2].includes(c)) return '一部曇り';
  if (c === 3) return '曇り';
  if ([45, 48].includes(c)) return '霧';
  if ([51,53,55,56,57].includes(c)) return '霧雨';
  if ([61,63,65,66,67].includes(c)) return '雨';
  if ([71,73,75,77].includes(c)) return '雪';
  if (c >= 80 && c <= 82) return 'にわか雨';
  if (c >= 85 && c <= 86) return '雪';
  if ([95,96,97,99].includes(c)) return '雷雨';
  return '不明';
}
function weatherIcon() {
  const label = condition();
  if (label === '晴れ') return weather.isDay ? 'sun' : 'moon';
  if (label === '霧') return 'fog';
  if (label === '雪') return 'snow';
  if (['霧雨','雨','にわか雨','雷雨'].includes(label)) return 'rain';
  if (['一部曇り','曇り'].includes(label)) return 'cloud';
  return 'unknown';
}
function weatherValue(key, suffix = '') {
  return weather.status !== 'empty' && Number.isFinite(weather[key]) ? `${weather[key]}${suffix}` : `--${suffix}`;
}
function textValue(n, provider) {
  if (n.text !== undefined) return n.text;
  if(n.key.startsWith('calendar_') || n.key.startsWith('event_')) {
    if(n.key==='calendar_state')return calendar.sample?`サンプル · ${stateLabels[calendar.state]}${calendar.valid&&!calendar.events.length?' · 予定なし':''}`:stateLabels[calendar.state];
    if(n.key==='calendar_page'){const count=calendarCount();return count?`${calendarOffset+1}–${Math.min(calendarOffset+2,count)} / ${count} ↑↓`:'';}
    if(n.key==='calendar_updated')return `更新 ${calendar.updated || '未提供'}`;
    const slot=n.key.split(':')[1], index=slot===undefined?selectedEvent:calendarOffset+Number(slot);
    const event=index<calendarCount()?calendar.events[index]:null;
    if(!event)return '';
    if(n.key.startsWith('event_title'))return event.title || '(無題)';
    if(n.key.startsWith('event_time'))return event.time;
    if(n.key==='event_location')return event.location || '場所なし';
    if(n.key==='event_description')return event.description || '説明なし';
  }
  const p = providers[n.provider === undefined ? provider : n.provider], percent = quotaPercent(p);
  switch (n.key) {
    case 'clock': return clock;
    case 'date': return '2026年10月8日(木)';
    case 'date_short': return '10月8日(木)';
    case 'temperature': return weatherValue('temp', '°C');
    case 'feels': return weatherValue('feels', '°');
    case 'humidity': return weatherValue('humidity', '%');
    case 'rain': return weatherValue('rain', '%');
    case 'highlow': return `↑ ${weatherValue('high', '°')}   ↓ ${weatherValue('low', '°')}`;
    case 'highlow_short': return `${weatherValue('high', '°')} / ${weatherValue('low', '°')}`;
    case 'condition': return condition();
    case 'home_condition': return weather.status === 'stale' ? `更新失敗・${condition()}` : condition();
    case 'weather_updated': return weather.status === 'empty' ? 'Wi-Fi設定をご確認ください' : `最終更新 ${weather.updated}`;
    case 'service': return p.name;
    case 'balance': return balance(p);
    case 'detail_main': if(p.cursorBuckets&&percent===null)return '--'; return !p.valid?'--':p.unlimited?'∞':percent === null ? amount(p.remaining) : `${percent}%`;
    case 'detail_balance': if(p.id==='codex'||p.id==='claude')return ''; return p.cursorBuckets&&p.valid?'Cursor Models 残り':balance(p);
    case 'detail_unit': if(p.id==='cursor')return ''; if((p.id==='codex'||p.id==='claude')&&!p.demo&&p.valid)return quotaWindow({...p,unit:''}); return !p.valid?'':p.demo?(p.has_total?'credits':'上限未提供'):quotaWindow(p);
    case 'service_state': return p.demo?'デモ値':stateLabels[p.state];
    case 'quota_status': return providers.some(p=>p.demo)?'デモ含む':'実データ';
    case 'reset': if(p.cursorBuckets&&p.demo)return `リセット ${p.reset} (デモ)`; return `リセット ${p.demo?'未提供':p.reset || '未提供'}`;
    case 'secondary_reset': if(p.cursorBuckets&&p.valid)return p.secondary&&Number.isFinite(p.secondary.percent)?`Other Models 残り ${Math.max(0,Math.min(100,p.secondary.percent))}%`:'Other Models 未提供'; return !p.demo&&p.secondary?`${p.secondary.minutes/1440}日 残り ${p.secondary.percent}% ${p.secondary.reset || '未提供'}`:'';
    case 'updated': return `更新 ${p.demo?'デモ値':p.updated || '未提供'}`;
    default: return '--';
  }
}
function plate(c, x, y, w, h, alpha = .8, border = '#c9aa6d66') {
  c.save(); c.beginPath(); c.roundRect(x, y, w, h, 11);
  c.fillStyle = `rgba(1,17,24,${alpha})`; c.fill();
  c.lineWidth = .8; c.strokeStyle = border; c.stroke(); c.restore();
}
function paintText(c, n, str, output) {
  let size = n.size || 14;
  if(size<25)size+=SCENE.small_text_increment;
  c.save(); c.textAlign = 'center'; c.textBaseline = 'middle';
  c.font = `${n.weight || 500} ${size}px Shanhai`;
  if (size >= 25 && n.w && c.measureText(str).width > n.w) {
    size *= n.w / c.measureText(str).width;
    c.font = `${n.weight || 500} ${size}px Shanhai`;
  }
  c.lineJoin = 'round'; c.strokeStyle = '#001016'; c.lineWidth = size >= 25 ? 2.8 : 2;
  c.shadowColor = '#000'; c.shadowBlur = ['date','home_condition','highlow'].includes(n.key) ? 0 : size >= 25 ? 6 : 3;
  c.strokeText(str, n.x, n.y);
  c.fillStyle = n.color || '#fffaf0'; c.fillText(str, n.x, n.y);
  const m = c.measureText(str);
  output?.push({ text: str, size, box: [n.x - m.actualBoundingBoxLeft, n.y - m.actualBoundingBoxAscent, n.x + m.actualBoundingBoxRight, n.y + m.actualBoundingBoxDescent] });
  c.restore();
}
function icon(c, name, x, y, w, h, color = '#fffaf0') {
  if (name === 'wifi' && !network.connected) { name = 'wifi_off'; color = '#f1d088'; }
  const im = images[`icon:${name}:${color}`] || images[`icon:${name}:#fffaf0`];
  if (im) { c.save(); c.shadowColor = '#001016'; c.shadowBlur = 2; c.drawImage(im, x-w/2, y-h/2, w, h); c.restore(); }
}
function ring(c, n, p, output) {
  const percent = quotaPercent(p), r = n.r, accent = n.color || p.color;
  c.save(); c.lineWidth = n.width; c.lineCap = 'round';
  const metal = c.createRadialGradient(n.x, n.y, r-n.width, n.x, n.y, r+n.width);
  metal.addColorStop(0, '#081b24'); metal.addColorStop(.3, '#5e746c'); metal.addColorStop(.53, '#29434b'); metal.addColorStop(1, '#071b23');
  c.strokeStyle = metal; c.beginPath(); c.arc(n.x,n.y,r,0,2*Math.PI); c.stroke();
  if (percent !== null && percent > 0) {
    const glow = c.createConicGradient(-Math.PI/2, n.x, n.y);
    glow.addColorStop(0, '#f2d694'); glow.addColorStop(.32,accent); glow.addColorStop(.8,accent); glow.addColorStop(1,'#eaffd8');
    c.strokeStyle = glow; c.shadowColor = accent; c.shadowBlur = 3;
    c.beginPath(); c.arc(n.x,n.y,r,-Math.PI/2,-Math.PI/2+2*Math.PI*percent/100); c.stroke();
  }
  output?.push({ text: `${p.name} ${percent === null ? '上限未提供' : `残り ${percent}%`}`, box: null });
  c.restore();
}
function wrappedLines(c,str,width) {
  const lines=[];
  for(const paragraph of str.split('\n')){
    let line='';
    for(const char of paragraph){if(line&&c.measureText(line+char).width>width){lines.push(line);line='';}line+=char;}
    lines.push(line);
  }
  return lines;
}
function calendarTextHeight(n, detail){
  return calendarLineHeight*((n.key.startsWith('event_title') || (detail&&n.key==='event_description'))?2:1);
}
function paintCalendarText(c,n,str,output,detail=false){
  const height=calendarTextHeight(n,detail);
  c.save();c.font='500 16px Shanhai';let lines=wrappedLines(c,str,n.w-2);
  if(!detail){
    const count=height/calendarLineHeight;
    if(lines.length>count){lines=lines.slice(0,count);let last=lines[count-1];while(last&&c.measureText(last+'…').width>n.w-2)last=last.slice(0,-1);lines[count-1]=last+'…';}
  }
  c.beginPath();c.rect(n.x-n.w/2,n.y-height/2,n.w,height);c.clip();
  c.textAlign='left';c.textBaseline='alphabetic';c.fillStyle=n.color || '#fffaf0';
  const offset=detail&&n.key==='event_description'?Math.min(descriptionOffset,Math.max(0,lines.length*calendarLineHeight-height)):0;
  lines.forEach((line,i)=>c.fillText(line,n.x-n.w/2,n.y-height/2+calendarLineHeight-calendarBaseline+i*calendarLineHeight-offset));c.restore();
  output?.push({text:str,size:16,box:[n.x-n.w/2,n.y-height/2,n.x+n.w/2,n.y+height/2],lines:lines.length,height});
}
function renderPage(id, c, provider = selectedProvider, output = []) {
  const pg = SCENE.pages.find(p => p.id === id);
  c.fillStyle='#000';c.fillRect(0,0,360,360); c.drawImage(images[`bg:${pg.background}`],...SCENE.background_offset,360,360);
  for (const n of pg.nodes) {
    const ix = n.provider === 'selected' ? provider : n.provider;
    if (n.type === 'text') {
      const str=textValue({...n,provider:ix},provider);
      if(id==='event'&&n.key?.startsWith('event_'))paintCalendarText(c,n,str,output,true);
      else if(id==='calendar'&&n.key?.startsWith('event_'))paintCalendarText(c,n,str,output);
      else paintText(c,id==='detail'&&(provider===0||provider===2)&&n.key==='detail_unit'?{...n,size:15,y:193}:n,str,output);
    }
    else if (n.type === 'panel') plate(c,n.x,n.y,n.w,n.h,n.alpha);
    else if (n.type === 'icon') {
      icon(c,n.icon || weatherIcon(),n.x,n.y,n.w,n.h,n.color || '#fffaf0');
      if (n.icon === 'wifi') output.push({text:`Wi-Fi ${network.connected?'接続済み':'未接続'}`,box:null});
    }
    else if (n.type === 'logo') c.drawImage(images[`logo:${providers[ix].id}`],n.x-n.w/2,n.y-n.h/2,n.w,n.h);
    else if (n.type === 'ring') ring(c,n,providers[ix],output);
    else if (n.type === 'line') { c.fillStyle='#cbbd8a44'; c.fillRect(n.x,n.y,n.w,.65); }
  }
  if (id === 'weather' && weather.status === 'stale') {
    const y=185;
    plate(c,99,y-8,181,16,.92,'#d9ae6366');
    paintText(c,{x:189,y,size:10,color:'#f1d088'},'更新に失敗・前回のデータ',output);
  }
  return output;
}
const overlayHits = [];
function overlayButton(c, output, text, y, action, danger = false) {
  plate(c,83,y-17,194,34,.97,danger ? '#d7846d' : '#c9aa6d99');
  paintText(c,{x:180,y,size:13,weight:600,color:danger?'#f4b29a':'#f1d088'},text,output);
  overlayHits.push({x:83,y:y-17,w:194,h:34,action,label:text});
}
function renderOverlay(c, output) {
  overlayHits.length=0;
  c.save(); c.beginPath(); c.arc(180,180,160,0,Math.PI*2); c.fillStyle='#001018ed'; c.fill(); c.restore();
  icon(c,'back',81,73,16,20,'#f1d088'); overlayHits.push({x:58,y:51,w:42,h:44,action:'close'});
  if (overlay === 'network') {
    paintText(c,{x:181,y:73,size:17,weight:600},'ネットワーク',output);
    icon(c,'wifi',180,113,32,30,network.connected?'#5ce8ce':'#f1d088');
    paintText(c,{x:180,y:148,size:15,weight:600,color:network.connected?'#5ce8ce':'#f1d088'},network.connected?'接続済み':'未接続',output);
    paintText(c,{x:180,y:173,size:11,color:'#d3d9d4'},network.saved ? network.ssid : '保存済みネットワークなし',output);
    overlayButton(c,output,network.connected?'切断':'再接続',213,network.connected?'disconnect':'reconnect');
    overlayButton(c,output,network.saved?'ネットワークを忘れる':'BLEで接続設定',258,network.saved?'forget':'pairing');
    paintText(c,{x:180,y:291,size:10,w:232,color:'#bfcfc7'},'切断しても設定は保存されます',output);
    paintText(c,{x:180,y:309,size:9,color:'#96aba5'},'プレビュー・接続状態はデモ',output);
  } else if (overlay === 'confirm') {
    paintText(c,{x:180,y:82,size:18,weight:600},'Wi-Fiを忘れる',output);
    icon(c,'warning',180,121,30,30,'#f1d088');
    paintText(c,{x:180,y:160,size:12,w:237},'保存済みの接続情報を削除し、',output);
    paintText(c,{x:180,y:183,size:12},'BLEで再設定します。',output);
    overlayButton(c,output,'削除して再設定',234,'confirm-forget',true);
    overlayButton(c,output,'キャンセル',280,'cancel-forget');
  } else if (overlay === 'pairing') {
    paintText(c,{x:180,y:70,size:19,weight:600},'Wi-Fi設定',output);
    paintText(c,{x:180,y:95,size:10,w:224},'ESP BLE Provisioningでスキャン',output);
    c.save();c.imageSmoothingEnabled=false;c.drawImage(images.qr,89,111,182,182);c.restore();
    paintText(c,{x:180,y:305,size:10,color:'#f1d088'},'プレビュー専用・接続できません',output);
    paintText(c,{x:180,y:327,size:11,color:'#d3d9d4'},'キャンセル',output);
    overlayHits.push({x:121,y:313,w:118,h:28,action:'close'});
  } else if (overlay === 'error') {
    paintText(c,{x:180,y:77,size:17,weight:600},'Wi-Fi設定',output);
    icon(c,'warning',180,125,40,36,'#f3aa83');
    paintText(c,{x:180,y:171,size:18,weight:600},'接続できません',output);
    paintText(c,{x:180,y:202,size:11,w:247},'パスワードと2.4GHz Wi-Fiを',output);
    paintText(c,{x:180,y:223,size:11},'ご確認ください。',output);
    overlayButton(c,output,'再試行',270,'pairing');
    paintText(c,{x:180,y:313,size:11,color:'#d3d9d4'},'キャンセル',output);
    overlayHits.push({x:121,y:297,w:118,h:30,action:'close'});
  }
}
function draw() {
  if (!window.previewReady) return;
  clampCalendar();
  const pg=SCENE.pages.find(p=>p.id===currentPage);
  const output=[];
  if (overlay) {
    ctx.fillStyle='#000';ctx.fillRect(0,0,360,360);
    ctx.drawImage(images[`bg:${pg.background}`],...SCENE.background_offset,360,360);
    renderOverlay(ctx,output);
  } else renderPage(currentPage,ctx,selectedProvider,output);
  document.getElementById('readout').textContent=output.map(t=>t.text).join(' ');
  canvas.setAttribute('aria-label',overlay?`EchoEar ${overlay}画面`:`EchoEar ${pg.title}`);
  window.layoutAudit=output.filter(t=>t.box);
  document.getElementById('screen-name').textContent=overlay?'Wi-Fi 設定':`${String(pageIds.indexOf(currentPage)+1).padStart(2,'0')}　${pg.title}`;
  document.getElementById('screen-caption').textContent=overlay?'BLE設定・切断・再接続・忘れる':pg.caption;
  document.getElementById('hint').textContent=overlay?'圆屏内按钮可点击；下方按钮可演示配网结果。':currentPage==='detail'?'轻点圆环切换服务；轻点底部信息返回 AI 总览。':currentPage==='quota'?'轻点服务图标进入详情。':currentPage==='calendar'?'每屏两条；上下滑动或滚轮查看更多，轻点查看详情；左右切屏。':currentPage==='event'?'左上角返回列表；在描述区域滚轮或触控拖动阅读。':currentPage==='home'?'轻点天气、AI、日历或上方 Wi-Fi 图标。':'左右滑动切换页面；待机时轻点唤醒。';
  const actions=document.getElementById('screen-actions');
  const hadFocus=actions.contains(document.activeElement), focusedAction=document.activeElement.dataset?.action;
  actions.replaceChildren();
  const labels={network:'Wi-Fi設定',weather:'天気詳細',quota:'AI利用状況','next-provider':'次のサービス',close:'閉じる',calendar:'カレンダー',home:'ホームへ'};
  const hits=overlay?overlayHits:currentPage==='standby'?[{action:'home',label:'ホームへ'}]:pg.hits.filter(h=>!h.action.startsWith('event:')||(calendar.valid&&calendarOffset+Number(h.action.split(':')[1])<calendarCount()));
  if(!overlay&&currentPage==='calendar'){if(calendarOffset>0)hits.push({action:'calendar-prev',label:'前の2件'});if(calendarOffset+2<calendarCount())hits.push({action:'calendar-next',label:'次の2件'});}
  for(const [name,hit] of new Map(hits.map(h=>[h.action,h]))) {
    const button=document.createElement('button');button.className='action';button.dataset.action=name;
    button.textContent=hit.label || (name.startsWith('provider:')?providers[Number(name.split(':')[1])].name:name.startsWith('event:')?`予定 ${calendarOffset+Number(name.split(':')[1])+1}`:labels[name] || name);
    button.onclick=()=>overlay?networkAction(name):action(name);
    actions.appendChild(button);
  }
  if(hadFocus) {
    const target=overlay==='confirm'?'cancel-forget':focusedAction;
    (Array.from(actions.children).find(b=>b.dataset.action===target) || actions.firstElementChild || canvas).focus();
  }
  for (const p of SCENE.pages) {
    const thumb=document.getElementById(`thumb-${p.id}`);
    renderPage(p.id,thumb.getContext('2d'));
    thumb.parentElement.classList.toggle('active',p.id===currentPage);
  }
}
function syncControls() {
  const p=providers[selectedProvider];
  document.getElementById('provider').value=selectedProvider;
  if(document.getElementById('quota-shape'))document.getElementById('quota-shape').value=p.demo?'demo':p.state==='ready'?(p.unit==='USD'?'dollars':'percent'):p.state;
  for(const key of ['title','location','description'])if(document.getElementById(`event-${key}`))document.getElementById(`event-${key}`).value=calendar.events[selectedEvent]?.[key] || '';
  document.getElementById('remaining').value=p.remaining;
  document.getElementById('total').value=p.total;
  document.getElementById('has-total').checked=p.has_total;
  document.getElementById('weather-state').value=weather.status;
  if(document.getElementById('calendar-state'))document.getElementById('calendar-state').value=calendar.valid&&!calendar.events.length?'empty':calendar.state;
  document.getElementById('temperature').value=weather.temp;
  document.getElementById('temp-output').value=`${weather.temp}°C`;
  document.getElementById('clock-input').value=clock;
}
function navigate(id, provider) {
  if (!pageIds.includes(id)) return;
  if (Number.isInteger(provider) && provider>=0 && provider<providers.length) selectedProvider=provider;
  currentPage=id;overlay=null;activity();syncControls();draw();
}
function setQuota(index, remaining, total, hasTotal) {
  if (!Number.isInteger(index) || !providers[index] || !Number.isFinite(remaining) || !Number.isFinite(total) || remaining<0 || total<0 || remaining>1e9 || total>1e9) return false;
  Object.assign(providers[index],{cursorBuckets:false,demo:true,valid:true,state:'ready',percentOnly:false,percent:undefined,unlimited:false,unit:'credits',remaining:Math.trunc(remaining),total:Math.trunc(total),has_total:!!hasTotal&&total>0});
  syncControls();draw();return true;
}
function setWeather(update) {
  const next={...weather,...update};
  if (!['ready','stale','empty'].includes(next.status)) return false;
  for (const k of ['temp','feels','humidity','rain','high','low','code']) if (!Number.isFinite(next[k])) return false;
  if (next.humidity<0 || next.humidity>100 || next.rain<0 || next.rain>100) return false;
  weather=next;syncControls();draw();return true;
}
function openNetwork() { overlay='network';activity();draw(); }
function closeOverlay() { overlay=null;activity();draw(); }
function networkAction(action) {
  activity();
  switch(action) {
    case 'disconnect':network.connected=false;network.autoReconnect=false;overlay='network';break;
    case 'reconnect':if(network.saved){network.connected=true;network.autoReconnect=true;overlay='network';}else overlay='pairing';break;
    case 'forget':overlay='confirm';break;
    case 'confirm-forget':network={saved:false,connected:false,autoReconnect:false,ssid:''};overlay='pairing';break;
    case 'cancel-forget':overlay='network';break;
    case 'pairing':overlay='pairing';break;
    case 'provision-success':network={saved:true,connected:true,autoReconnect:true,ssid:'EchoEar-Demo'};overlay='network';break;
    case 'provision-failure':overlay='error';break;
    case 'close':closeOverlay();return;
  }
  draw();
}
function resetDemo() {
  providers=structuredClone(SCENE.providers);weather=structuredClone(SCENE.weather);calendar=structuredClone(SCENE.calendar);selectedEvent=0;descriptionOffset=0;calendarOffset=0;
  network={saved:true,connected:true,autoReconnect:true,ssid:'EchoEar-Demo'};
  clock='10:28';selectedProvider=2;navigate('home');
}
function tick() {
  if (idleEnabled && !overlay && currentPage!=='standby' && performance.now()-lastActivity>=45000) navigate('standby');
}
function changePage(delta) { navigate(pageIds[(pageIds.indexOf(currentPage)+delta+pageIds.length)%pageIds.length]); }
function action(name) {
  if(name==='network')openNetwork();
  else if(name==='calendar-next')changeCalendarPage(1);
  else if(name==='calendar-prev')changeCalendarPage(-1);
  else if(name.startsWith('event:')){const index=calendarOffset+Number(name.split(':')[1]);if(calendar.valid&&calendar.events[index]){selectedEvent=index;descriptionOffset=0;navigate('event');}}
  else if(name.startsWith('provider:'))navigate('detail',Number(name.split(':')[1]));
  else if(name==='next-provider')navigate('detail',(selectedProvider+1)%providers.length);
  else navigate(name);
}
canvas.addEventListener('pointerdown',e=>{pointerStart={x:e.clientX,y:e.clientY};canvas.setPointerCapture(e.pointerId);});
canvas.addEventListener('pointercancel',()=>{pointerStart=null;});
canvas.addEventListener('pointerup',e=>{
  if(!pointerStart)return;
  const dx=e.clientX-pointerStart.x,dy=e.clientY-pointerStart.y;pointerStart=null;activity();
  if(!overlay&&currentPage==='calendar'&&Math.abs(dy)>45&&Math.abs(dy)>Math.abs(dx)){changeCalendarPage(dy<0?1:-1);return;}
  if(!overlay&&Math.abs(dx)>45){changePage(dx<0?1:-1);return;}
  const rect=canvas.getBoundingClientRect(),x=(e.clientX-rect.left)*360/rect.width,y=(e.clientY-rect.top)*360/rect.height;
  if(overlay){const h=overlayHits.find(h=>x>=h.x&&x<=h.x+h.w&&y>=h.y&&y<=h.y+h.h);if(h)networkAction(h.action);return;}
  if(currentPage==='standby'){navigate('home');return;}
  const hit=SCENE.pages.find(p=>p.id===currentPage).hits.find(h=>x>=h.x&&x<=h.x+h.w&&y>=h.y&&y<=h.y+h.h);
  if(hit)action(hit.action);
});
document.getElementById('prev').onclick=()=>changePage(-1);
document.getElementById('next').onclick=()=>changePage(1);
document.getElementById('home').onclick=()=>navigate('home');
document.getElementById('network').onclick=openNetwork;
document.getElementById('provider').onchange=e=>{selectedProvider=Number(e.target.value);activity();syncControls();draw();};
for(const id of ['remaining','total','has-total'])document.getElementById(id).oninput=()=>{activity();setQuota(selectedProvider,Number(document.getElementById('remaining').value),Number(document.getElementById('total').value),document.getElementById('has-total').checked);};
document.getElementById('weather-state').onchange=e=>{activity();setWeather({status:e.target.value});};
document.getElementById('temperature').oninput=e=>{activity();setWeather({temp:Number(e.target.value)});};
document.getElementById('clock-input').oninput=e=>{if(/^\d{2}:\d{2}$/.test(e.target.value)){clock=e.target.value;activity();draw();}};
document.getElementById('idle-enabled').onchange=e=>{idleEnabled=e.target.checked;activity();};
document.getElementById('reset').onclick=resetDemo;
document.getElementById('simulate-success').onclick=()=>networkAction('provision-success');
document.getElementById('simulate-failure').onclick=()=>networkAction('provision-failure');
document.getElementById('show-pairing').onclick=()=>networkAction('pairing');
document.addEventListener('keydown',e=>{
  if(e.target.matches('input,select,button'))return;
  if(e.key==='Escape')closeOverlay();
  else if(!overlay&&currentPage==='calendar'&&(e.key==='ArrowUp'||e.key==='ArrowDown')){e.preventDefault();changeCalendarPage(e.key==='ArrowDown'?1:-1);}
  else if(!overlay&&e.key==='ArrowLeft')changePage(-1);
  else if(!overlay&&e.key==='ArrowRight')changePage(1);
  else if((e.key==='Enter'||e.key===' ')&&e.target===canvas){e.preventDefault();if(currentPage==='standby')navigate('home');else if(currentPage==='detail')action('next-provider');else openNetwork();}
});
for(const p of SCENE.pages){
  const button=document.createElement('button');button.className='page-card';button.dataset.page=p.id;
  button.innerHTML=`<canvas id="thumb-${p.id}" width="360" height="360" aria-hidden="true"></canvas><span>${p.title}</span>`;
  button.onclick=()=>navigate(p.id);document.getElementById('page-grid').appendChild(button);
}
providers.forEach((p,i)=>{const option=document.createElement('option');option.value=i;option.textContent=p.name;document.getElementById('provider').appendChild(option);});
function addCloudControls(){
  const section=document.createElement('section');section.className='controls';
  section.innerHTML=`<h2>实数据形态 · 离线样例</h2><div class="rows"><label class="control">额度形态<select id="quota-shape"><option value="demo">旧版 credits 演示</option><option value="percent">利用窗百分比样例</option><option value="dollars">美元余额样例</option><option value="unconfigured">未接続</option><option value="auth">认证已过期</option><option value="loading">加载中</option><option value="stale">旧数据</option><option value="error">未支持 / 获取失败</option></select></label><label class="control">日历状态<select id="calendar-state"><option value="ready">六条样例日程</option><option value="empty">没有日程</option><option value="unconfigured">未接続</option><option value="loading">加载中</option><option value="stale">旧日程</option><option value="auth">认证已过期</option><option value="error">获取失败</option></select></label><label class="control">日程标题<input id="event-title" maxlength="100" value="製品レビュー・画面と音声"></label><label class="control">日程地点<input id="event-location" maxlength="100" value="会議室 山海 / 東京"></label><label class="control">日程说明<input id="event-description" maxlength="350" value="画面デザイン、音声の応答と次のリリースを確認します。"></label></div><p class="note">百分比取服务返回的 remaining_percent；旧演示按 remaining ÷ total × 100。余额保留原单位，不能当作 token。双窗口分别显示，重置和更新时间来自对应窗口。日历使用设备本地时区，全日事件显示「終日」。仅渲染样例数据；不包含帐号信息或凭据。</p>`;
  document.getElementById('page-grid').parentElement.appendChild(section);
  document.getElementById('quota-shape').onchange=e=>{
    const p=providers[selectedProvider],shape=e.target.value;
    if(shape==='demo'){setQuota(selectedProvider,p.remaining,p.total,p.has_total);return;}
    Object.assign(p,{cursorBuckets:false,demo:false,state:shape==='stale'?'stale':['percent','dollars'].includes(shape)?'ready':shape,valid:['percent','dollars','stale'].includes(shape),percentOnly:shape!=='dollars',percent:shape==='dollars'?undefined:62,unlimited:false,unit:shape==='dollars'?'USD':'',window_minutes:300,secondary:shape==='dollars'?null:{minutes:10080,percent:78,reset:'10/15 10:20'},reset:'10/8 15:20',updated:'10/8 10:20',remaining:18.5,total:0,has_total:false});activity();syncControls();draw();
  };
  document.getElementById('calendar-state').onchange=e=>{const state=e.target.value;calendar={...structuredClone(SCENE.calendar),state:state==='empty'?'ready':state,valid:['ready','empty','stale'].includes(state)};if(state==='empty')calendar.events=[];clampCalendar();activity();syncControls();draw();};
  for(const key of ['title','location','description'])document.getElementById(`event-${key}`).oninput=e=>{if(calendar.events[selectedEvent])calendar.events[selectedEvent][key]=e.target.value;activity();draw();};
}
canvas.addEventListener('wheel',e=>{if(currentPage==='calendar'&&!overlay){e.preventDefault();changeCalendarPage(e.deltaY>0?1:-1);return;}if(currentPage==='event'&&!overlay){e.preventDefault();descriptionOffset=Math.max(0,descriptionOffset+e.deltaY);activity();draw();}},{passive:false});
let descriptionDrag=null;
canvas.addEventListener('pointerdown',e=>{const r=canvas.getBoundingClientRect();if(currentPage==='event'&&!overlay&&(e.clientY-r.top)*360/r.height>230)descriptionDrag=e.clientY;});
canvas.addEventListener('pointerup',e=>{if(descriptionDrag!==null){descriptionOffset=Math.max(0,descriptionOffset+descriptionDrag-e.clientY);descriptionDrag=null;draw();}});
addCloudControls();
document.querySelector('.footer').textContent='轻点服务图标查看额度详情 · 轻点日历日程查看详情 · 左右滑动切页 · 待机轻点唤醒。所有额度、日程、时间、天气和网络操作均为离线样例。';
async function init() {
  const sources={...ASSETS};
  for(const [name,paths] of Object.entries(iconPaths))for(const color of ['#fffaf0','#f1d088','#f3d080','#5ce8ce','#f3aa83']) {
    const svg=`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="${color}" color="${color}" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">${paths}</svg>`;
    sources[`icon:${name}:${color}`]=`data:image/svg+xml,${encodeURIComponent(svg)}`;
  }
  await Promise.all(Object.entries(sources).map(([key,src])=>new Promise((resolve,reject)=>{const im=new Image();im.onload=()=>{images[key]=im;resolve();};im.onerror=()=>reject(new Error(`Asset failed: ${key}`));im.src=src;})));
  await Promise.all([document.fonts.load('500 14px Shanhai'),document.fonts.load('700 48px Shanhai')]);
  window.previewReady=true;resetDemo();setInterval(tick,1000);
}
init().catch(e=>{document.getElementById('hint').textContent=e.message;throw e;});
