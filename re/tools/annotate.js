// Annotates a dumpbin /DISASM listing with string literals, import names
// and function labels (call targets). Usage: node annotate.js <exe> <asm> <out>
const fs = require('fs');
const [exePath, asmPath, outPath] = process.argv.slice(2);
const b = fs.readFileSync(exePath);

const pe = b.readUInt32LE(0x3c);
const nsec = b.readUInt16LE(pe + 6), optsz = b.readUInt16LE(pe + 20);
const opt = pe + 24, imageBase = b.readUInt32LE(opt + 28);
const secs = [];
for (let i = 0; i < nsec; i++) {
  const o = opt + optsz + i * 40;
  secs.push({ name: b.toString('latin1', o, o + 8).replace(/\0.*$/, ''), vsize: b.readUInt32LE(o + 8), va: b.readUInt32LE(o + 12), rsize: b.readUInt32LE(o + 16), raw: b.readUInt32LE(o + 20) });
}
const rva2off = r => { for (const s of secs) if (r >= s.va && r < s.va + s.rsize) return r - s.va + s.raw; return -1; };
const cstr = o => { let e = o; while (b[e]) e++; return b.toString('latin1', o, e); };

// imports: IAT slot VA -> "dll!name"
const imports = new Map();
let io = rva2off(b.readUInt32LE(opt + 104));
for (;; io += 20) {
  const oft = b.readUInt32LE(io), nameRva = b.readUInt32LE(io + 12), ft = b.readUInt32LE(io + 16);
  if (!nameRva) break;
  const dll = cstr(rva2off(nameRva)).replace(/\.dll$/i, '');
  for (let k = 0; ; k++) {
    const v = b.readUInt32LE(rva2off(oft || ft) + k * 4);
    if (!v) break;
    imports.set(imageBase + ft + k * 4, `${dll}!${v & 0x80000000 ? '#' + (v & 0xffff) : cstr(rva2off(v) + 2)}`);
  }
}

// printable C string at a VA (only for initialized data sections)
function strAt(va) {
  const off = rva2off(va - imageBase);
  if (off < 0) return null;
  const sec = secs.find(s => va - imageBase >= s.va && va - imageBase < s.va + s.rsize);
  if (!sec || sec.name === '.text') return null;
  let e = off;
  while (e < b.length && b[e] && e - off < 300) {
    const c = b[e];
    if (!(c >= 32 && c < 127) && c !== 10 && c !== 13 && c !== 9) return null;
    e++;
  }
  if (b[e] !== 0 || e - off < 3) return null;
  return b.toString('latin1', off, e).replace(/\n/g, '\\n').replace(/\r/g, '\\r');
}

const lines = fs.readFileSync(asmPath, 'latin1').split(/\r?\n/);
const callTargets = new Set();
for (const l of lines) {
  const m = /^\s+([0-9A-F]{8}): call\s+([0-9A-F]{8})$/.exec(l);
  if (m) callTargets.add(m[2]);
}

const out = [];
for (const l of lines) {
  const m = /^\s+([0-9A-F]{8}): (.*)$/.exec(l);
  if (!m) { out.push(l); continue; }
  const [, addr, ins] = m;
  if (callTargets.has(addr)) out.push(`\nsub_${addr}:`);
  const notes = [];
  for (const h of ins.match(/\b[0-9A-F]{6,8}h?\b/g) || []) {
    const va = parseInt(h, 16);
    if (imports.has(va)) notes.push(imports.get(va));
    else { const s = strAt(va); if (s) notes.push(JSON.stringify(s)); }
  }
  out.push(`  ${addr}: ${ins}${notes.length ? '   ; ' + notes.join(' ') : ''}`);
}
fs.writeFileSync(outPath, out.join('\n'));
console.log(`${callTargets.size} functions, ${imports.size} imports`);
