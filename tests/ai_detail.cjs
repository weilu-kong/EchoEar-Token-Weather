const assert=require('node:assert/strict');
const fs=require('node:fs');
const {chromium}=require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const root=require('node:path').resolve(__dirname,'..');
(async()=>{
 const browser=await chromium.launch({headless:true,...(process.env.CHROME_PATH?{executablePath:process.env.CHROME_PATH}:{})});
 try {
  const page=await browser.newPage({viewport:{width:1280,height:1100}});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.setContent(fs.readFileSync(root+'/SHANHAI_離線プレビュー.html','utf8'),{waitUntil:'load'});
  await page.waitForFunction(()=>window.previewReady);
  await page.evaluate(()=>{idleEnabled=false;navigate('detail',3);});
  let readout=await page.locator('#readout').textContent();
  assert.doesNotMatch(readout,/TEAM 使用率/);assert.match(readout,/32%/);assert.match(readout,/Cursor Models 残り/);assert.match(readout,/Other Models 残り 89%/);
  assert.equal(await page.evaluate(()=>textValue({key:'detail_main'},3)),'32%');
  assert.match(readout,/Cursor 残り 32%/,'Detail ring also represents remaining quota');
  await page.locator('#lcd').screenshot({path:root+'/preview/cursor_buckets.png'});
  await page.evaluate(()=>{providers[3].percent=100;providers[3].secondary={percent:0};draw();});
  readout=await page.locator('#readout').textContent();
  assert.equal(await page.evaluate(()=>textValue({key:'detail_main'},3)),'100%');assert.match(readout,/Other Models 残り 0%/);
  await page.evaluate(()=>{providers[3].percent=0;providers[3].secondary={percent:-10};draw();});
  assert.equal(await page.evaluate(()=>textValue({key:'detail_main'},3)),'0%');
  assert.match(await page.locator('#readout').textContent(),/Other Models 残り 0%/);
  await page.evaluate(()=>{providers[3].percent=undefined;providers[3].secondary=null;draw();});
  assert.match(await page.locator('#readout').textContent(),/Other Models 未提供/);
  assert.equal(await page.evaluate(()=>textValue({key:'detail_main'},3)),'--','Missing primary quota must not become a balance');
  await page.evaluate(()=>setQuota(3,25,100,true));
  assert.equal(await page.evaluate(()=>providers[3].cursorBuckets),false);

  await page.evaluate(()=>{providers[0]={...providers[0],demo:false,unit:'',reset:'10/9 15:20',percent:75,percentOnly:true,window_minutes:300,secondary:{minutes:10080,percent:89,reset:'10/20 09:00'}};navigate('detail',0);});
  readout=await page.locator('#readout').textContent();
  assert.equal(await page.evaluate(()=>textValue({key:'detail_balance'},0)),'');assert.equal(await page.evaluate(()=>window.layoutAudit.find(n=>n.text.trim()==='5時間').size),17);assert.match(readout,/75%/);assert.match(readout,/5時間/);assert.match(readout,/7日 残り 89%/);
  await page.locator('#lcd').screenshot({path:root+'/preview/codex_repositioned.png'});
  await page.evaluate(()=>{providers[0]={...providers[0],unit:'credits',remaining:0,has_total:false,percentOnly:false};navigate('quota');});
  assert.equal(await page.evaluate(()=>textValue({key:'balance',provider:0},0)),'残り 75%');
  await page.evaluate(()=>{resetDemo();navigate('quota');});
  assert.equal(await page.evaluate(()=>textValue({key:'balance',provider:3},3)),'残り 32%');
  await page.evaluate(()=>{providers[2]={...providers[2],demo:false,valid:true,state:'ready',percent:78,percentOnly:true,window_minutes:300,unit:'',secondary:{minutes:10080,percent:68,reset:'10/14 02:00'}};navigate('detail',2);});
  assert.equal(await page.evaluate(()=>textValue({key:'detail_balance'},2)),'');
  assert.match(await page.locator('#readout').textContent(),/78%/);
  const claudeWindow=await page.evaluate(()=>window.layoutAudit.find(n=>n.text.trim()==='5時間'));
  assert.equal(claudeWindow.size,17);
  assert.ok(claudeWindow.box[1]<193 && claudeWindow.box[3]>193,'Claude window matches Codex position');

  await page.evaluate(()=>navigate('detail',0));
  const geometry=await page.evaluate(()=>SCENE.pages.find(p=>p.id==='detail').nodes.filter(n=>['secondary_reset','reset','updated'].includes(n.key)).sort((a,b)=>a.y-b.y));
  for(let i=1;i<geometry.length;i++)assert.ok(geometry[i].y-geometry[i-1].y>=22,'footer rows must allow native font height plus gap');
  // Tap the relocated ring, then the relocated timestamps.
  const b=await page.locator('#lcd').boundingBox();
  await page.mouse.click(b.x+220,b.y+171);
  assert.equal(await page.evaluate(()=>selectedProvider),1);
  await page.mouse.click(b.x+208,b.y+287);
  assert.equal(await page.evaluate(()=>currentPage),'quota');
  assert.deepEqual(errors,[]);
  console.log('PASS two Cursor buckets, Codex windows, relocated touch targets; screenshots are synthetic samples.');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1)});
