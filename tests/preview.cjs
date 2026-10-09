const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const root = path.resolve(__dirname, '..');
const html = path.join(root, 'SHANHAI_離線プレビュー.html');
assert(fs.existsSync(html), 'Offline preview has not been generated');

(async () => {
  const browser = await chromium.launch({
    headless: true,
    ...(process.env.CHROME_PATH ? { executablePath: process.env.CHROME_PATH } : {}),
  });
  try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 1100 } });
  const errors = [];
  const requests = [];
  const geometry = [];
  page.on('pageerror', e => errors.push(e.message));
  page.on('request', r => { if (/^https?:/.test(r.url())) requests.push(r.url()); });
  await page.setContent(fs.readFileSync(html, 'utf8'), { waitUntil: 'load' });
  await page.waitForFunction(() => window.previewReady === true);
  const readout = () => page.locator('#readout').textContent();
  const audit = async id => {
    const boxes = await page.evaluate(() => window.layoutAudit);
    for (const a of boxes) {
      for (const x of [a.box[0], a.box[2]]) for (const y of [a.box[1], a.box[3]])
        assert(Math.hypot(x - 180, y - 180) <= 171, `${id}: outside safe circle: ${a.text}`);
    }
    for (let i = 0; i < boxes.length; i++) for (let j = i + 1; j < boxes.length; j++) {
      const a = boxes[i], b = boxes[j];
      const overlapX = Math.min(a.box[2], b.box[2]) - Math.max(a.box[0], b.box[0]);
      const overlapY = Math.min(a.box[3], b.box[3]) - Math.max(a.box[1], b.box[1]);
      assert(!(overlapX > 2 && overlapY > 2), `${id}: text overlap: ${a.text} / ${b.text}`);
    }
    geometry.push({ id, textNodes: boxes.length, issues: 0 });
  };
  const pages = await page.evaluate(() => pageIds);
  assert.equal(pages.length, 7, 'Current delivery has seven pages');
  for (const id of pages) {
    if (id === 'standby') {
      await page.locator('#clock-input').fill('22:18');
      await page.evaluate(() => setWeather({ temp: 20, code: 3, isDay: false }));
    }
    await page.locator(`[data-page="${id}"]`).click();
    assert.equal(await page.evaluate(() => currentPage), id);
    await audit(id);
    if(id==='home') {
      const sizes=await page.evaluate(()=>window.layoutAudit.map(n=>({text:n.text,size:n.size})));
      assert.equal(sizes.find(n=>n.text==='10:28').size,44,'Large clock stays unchanged');
      assert.equal(sizes.find(n=>n.text==='東京').size,16,'City remains unchanged');
      assert.equal(sizes.find(n=>n.text==='2026年10月8日(木)').size,16,'Date increases another 2px');
      assert.equal(sizes.find(n=>n.text==='晴れ').size,17,'Condition increases another 2px');
      assert.equal(sizes.find(n=>n.text.includes('↑')).size,16,'High/low matches the city size');
      assert.equal(sizes.find(n=>n.text==='AI利用状況').size,15,'Footer text increases by 2px');
    }
    await page.locator('#lcd').screenshot({ path: path.join(root, 'preview', `${id}.png`) });
  }
  for (let provider = 0; provider < 4; provider++) {
    await page.evaluate(p => navigate('detail', p), provider);
    await audit(`detail-provider-${provider}`);
  }
  await page.evaluate(() => resetDemo());
  const lcdBox = await page.locator('#lcd').boundingBox();
  await page.mouse.move(lcdBox.x + 270, lcdBox.y + 180);
  await page.mouse.down();
  await page.mouse.move(lcdBox.x + 110, lcdBox.y + 180, { steps: 5 });
  await page.mouse.up();
  assert.equal(await page.evaluate(() => currentPage), 'weather', 'Canvas swipe changes page');
  await page.evaluate(() => resetDemo());
  await page.evaluate(() => { setQuota(0, 0, 500, true); navigate('detail', 0); });
  assert.match(await readout(), /0%/);
  await page.evaluate(() => setQuota(0, 500, 500, true));
  assert.match(await readout(), /100%/);
  await page.evaluate(() => { setQuota(1, 420, 0, false); navigate('detail', 1); });
  assert.match(await readout(), /420 credits/);
  assert(!/\d+%/.test(await readout()), 'Unknown total must not invent a percentage');
  assert.equal(await page.locator('#total').isDisabled(), false, 'Unknown total must still allow entering an available total');
  await page.locator('#total').fill('1000');
  await page.locator('#has-total').check();
  assert.match(await readout(), /42%/);
  await page.locator('#has-total').uncheck();
  assert(!/\d+%/.test(await readout()));
  await page.evaluate(() => { setWeather({ temp: 0, feels: 0, humidity: 0, rain: 0, high: 0, low: 0, code: 3, status: 'ready' }); navigate('weather'); });
  assert.match(await readout(), /0°C/);
  assert.match(await readout(), /0%/);
  await page.evaluate(() => setWeather({ status: 'empty' }));
  assert.match(await readout(), /--/);
  assert.equal(await page.evaluate(() => weatherIcon()), 'unknown', 'Empty weather must not imply sun/cloud/rain');
  await page.evaluate(() => setWeather({ status: 'ready', code: 999 }));
  assert.equal(await page.evaluate(() => weatherIcon()), 'unknown', 'Unrecognized weather code must not imply rain');
  await page.evaluate(() => setWeather({ status: 'ready', code: 52 }));
  assert.equal(await page.evaluate(() => weatherIcon()), 'unknown', 'Undefined in-range weather codes must stay neutral');
  await page.evaluate(() => { resetDemo(); networkAction('disconnect'); });
  assert.equal(await page.evaluate(() => network.saved), true);
  assert.equal(await page.evaluate(() => network.autoReconnect), false);
  await page.evaluate(() => closeOverlay());
  assert.match(await readout(), /Wi-Fi 未接続/);
  await page.evaluate(() => networkAction('reconnect'));
  assert.equal(await page.evaluate(() => network.connected), true);
  await page.evaluate(() => networkAction('forget'));
  assert.equal(await page.evaluate(() => network.saved), true, 'Forget needs confirmation');
  await page.evaluate(() => networkAction('confirm-forget'));
  assert.equal(await page.evaluate(() => network.saved), false);
  assert.equal(await page.evaluate(() => overlay), 'pairing');
  await page.evaluate(() => { lastActivity = performance.now() - 46000; tick(); });
  assert.notEqual(await page.evaluate(() => currentPage), 'standby', 'Do not hide pairing QR for idle timeout');
  await page.evaluate(() => networkAction('provision-success'));
  assert.equal(await page.evaluate(() => network.saved), true);
  await page.locator('#screen-actions [data-action="forget"]').focus();
  await page.keyboard.press('Enter');
  assert.equal(await page.evaluate(() => overlay), 'confirm');
  assert.equal(await page.evaluate(() => network.saved), true);
  assert.equal(await page.evaluate(() => document.activeElement.dataset.action), 'cancel-forget', 'Destructive confirmation defaults to cancel');
  await page.locator('#screen-actions [data-action="confirm-forget"]').focus();
  await page.keyboard.press('Enter');
  assert.equal(await page.evaluate(() => overlay), 'pairing', 'Forget can be confirmed with the keyboard');
  await page.evaluate(() => networkAction('provision-success'));
  await page.evaluate(() => { closeOverlay(); navigate('home'); lastActivity = performance.now() - 46000; tick(); });
  assert.equal(await page.evaluate(() => currentPage), 'standby');
  await page.locator('#lcd').click({ position: { x: 180, y: 180 } });
  assert.equal(await page.evaluate(() => currentPage), 'home');
  await page.evaluate(() => { openNetwork(); });
  await audit('network');
  await page.locator('#lcd').screenshot({ path: path.join(root, 'preview', '06_network.png') });
  await page.evaluate(() => networkAction('forget'));
  await audit('forget-confirm');
  await page.locator('#lcd').screenshot({ path: path.join(root, 'preview', '07_forget_confirm.png') });
  await page.evaluate(() => networkAction('confirm-forget'));
  await audit('pairing');
  await page.locator('#lcd').screenshot({ path: path.join(root, 'preview', '08_pairing.png') });
  await page.evaluate(() => networkAction('provision-failure'));
  await audit('pairing-error');
  await page.locator('#lcd').screenshot({ path: path.join(root, 'preview', '09_pairing_error.png') });
  await page.evaluate(() => { resetDemo(); setWeather({ status: 'stale' }); navigate('weather'); });
  await audit('weather-stale');
  await page.evaluate(() => { setWeather({status:'stale',code:0}); navigate('home'); });
  assert.match(await readout(), /更新失敗・晴れ/);
  await audit('home-weather-stale');
  await page.evaluate(() => navigate('weather'));
  await page.locator('#lcd').screenshot({ path: path.join(root, 'preview', '10_weather_stale.png') });
  await page.evaluate(() => { setWeather({ status: 'empty' }); });
  await audit('weather-empty');
  await page.locator('#lcd').screenshot({ path: path.join(root, 'preview', '11_weather_empty.png') });
  await page.evaluate(() => { resetDemo(); });
  await page.screenshot({ path: path.join(root, 'preview', 'browser.png'), fullPage: true });
  assert.deepEqual(errors, [], 'Browser JavaScript errors');
  assert.deepEqual(requests, [], 'Offline preview must not request remote resources');
  fs.writeFileSync(path.join(root, 'tests', 'results.json'), JSON.stringify({
    pages: pages.length, states: 6, jsErrors: errors, remoteRequests: requests, geometry,
    checks: ['navigation', 'swipe', 'quota-zero', 'quota-full', 'quota-no-total', 'quota-total-edit', 'weather-zero', 'weather-empty', 'weather-unknown-icon', 'disconnect', 'home-disconnected', 'reconnect', 'forget-confirmation', 'keyboard-confirmation', 'reprovision', 'pairing-idle', 'standby-wake'],
  }, null, 2) + '\n');
  console.log('PASS: 7 pages, network/error states, quota boundaries, Wi-Fi transitions, standby/wake; 0 JS errors; 0 remote requests.');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exit(1); });
