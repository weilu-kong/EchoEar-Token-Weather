const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const sharp = require(process.env.SHARP_MODULE || 'sharp');
const root = path.resolve(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'tools/preview.js'), 'utf8');
const match = source.match(/const iconPaths = (\{[\s\S]*?\n\});/);
const icons = vm.runInNewContext(`(${match[1]})`);
icons.wifi_off = icons.wifi + '<path d="M2 2 22 22"/>';
const destination = path.join(root, 'assets/firmware/icons');
fs.mkdirSync(destination, { recursive: true });
(async () => {
  for (const [name, paths] of Object.entries(icons)) {
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="white" color="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">${paths}</svg>`;
    await sharp(Buffer.from(svg)).resize(48,48).png().toFile(path.join(destination,`${name}.png`));
  }
  for (const id of ['codex','antigravity','claude','cursor'])
    await sharp(path.join(root,`assets/logos/${id}.svg`)).resize(64,64).png().toFile(path.join(destination,`${id}.png`));
})().catch(e=>{console.error(e);process.exit(1);});
