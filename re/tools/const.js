// Prints float/double constants at virtual addresses of Sculptris.exe.
// Usage: node const.js <exe> <addr>...
const fs = require('fs');
const [exe, ...addrs] = process.argv.slice(2);
const b = fs.readFileSync(exe);
const pe = b.readUInt32LE(0x3c), opt = pe + 24, optsz = b.readUInt16LE(pe + 20);
const secs = [];
for (let i = 0; i < b.readUInt16LE(pe + 6); i++) { const o = opt + optsz + i * 40; secs.push({ va: b.readUInt32LE(o + 12), raw: b.readUInt32LE(o + 20), rs: b.readUInt32LE(o + 16) }); }
const off = va => { const r = va - 0x400000; for (const s of secs) if (r >= s.va && r < s.va + s.rs) return r - s.va + s.raw; return -1; };
for (const a of addrs) {
  const o = off(parseInt(a, 16));
  if (o < 0) { console.log(a, 'bss (zero-initialized)'); continue; }
  console.log(a, 'f32', b.readFloatLE(o), ' f64', b.readDoubleLE(o), ' i32', b.readInt32LE(o));
}
