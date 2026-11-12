// Generate the GUI navigation icons from Lucide (ISC) as PNG.
//
//   cd tools
//   npm install
//   npm run icons
//
// Flaticon was not used because its free assets require attribution and its
// terms differ per asset; Lucide is an open-source navigation set that can be
// redistributed with the product. Swap the mapping below when licensed
// Flaticon PNG/SVG assets are supplied.
const fs = require('fs');
const path = require('path');
const sharp = require('sharp');

const root = path.resolve(__dirname, '..');
const lucide = path.dirname(require.resolve('lucide-static/package.json'));
const iconsDir = path.join(lucide, 'icons');
const outputDir = path.join(root, 'icons');

const mapping = {
  'pericia.png': ['scan-search', '#D4D4D4'],
  'avancado.png': ['sliders-horizontal', '#D4D4D4'],
  'sanitizacao.png': ['shield-check', '#D4D4D4'],
  'auditoria.png': ['clipboard-list', '#D4D4D4'],
  'wipe.png': ['trash-2', '#D4D4D4'],
  'kernel.png': ['cpu', '#D4D4D4'],
  'cloud.png': ['cloud', '#D4D4D4'],
  'hardware.png': ['monitor-cog', '#D4D4D4'],
  'recovery.png': ['images', '#D4D4D4'],
  'action_audit.png': ['clipboard-check', '#D4D4D4'],
  'action_driver.png': ['hard-drive', '#D4D4D4'],
  'action_help.png': ['circle-help', '#D4D4D4'],
  'action_read.png': ['file-search', '#D4D4D4'],
  'tab_hex.png': ['binary', '#D4D4D4'],
  'tab_table.png': ['table', '#D4D4D4'],
  'tab_timeline.png': ['activity', '#D4D4D4'],
  'acquisition.png': ['download', '#D4D4D4'],
  'artifacts.png': ['package-search', '#D4D4D4'],
  'e01.png': ['file-archive', '#D4D4D4'],
  'refresh.png': ['refresh-cw', '#D4D4D4'],
  'terminal.png': ['terminal', '#D4D4D4'],
  'search.png': ['search', '#D4D4D4']
};

(async () => {
  fs.mkdirSync(outputDir, { recursive: true });
  for (const [name, [source, color]] of Object.entries(mapping)) {
    const sourcePath = path.join(iconsDir, source + '.svg');
    if (!fs.existsSync(sourcePath)) throw new Error('missing Lucide icon: ' + source);
    const svg = fs.readFileSync(sourcePath, 'utf8').replace(/currentColor/g, color);
    await sharp(Buffer.from(svg)).resize(64, 64).png().toFile(path.join(outputDir, name));
    console.log('wrote icons/' + name);
  }
})();
