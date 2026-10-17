// Prints one function (from its sub_ label to the next) of the annotated
// listing, without int3/nop padding.
// Usage: node fn.js <annotated.asm> <addr> [more addrs...]
//        node fn.js <annotated.asm> --list <fromAddr> <toAddr>   (labels, sizes, strings, callees)
const fs = require('fs');
const [file, ...args] = process.argv.slice(2);
const lines = fs.readFileSync(file, 'latin1').split('\n');
const starts = [];
lines.forEach((l, i) => { const m = /^sub_([0-9A-F]{8}):/.exec(l); if (m) starts.push([parseInt(m[1], 16), i]); });
const body = idx => {
  const out = [];
  for (let i = starts[idx][1] + 1; i < (starts[idx + 1] ? starts[idx + 1][1] : lines.length); i++) {
    const l = lines[i];
    if (!l.trim() || /: (int\s+3|nop)$/.test(l) || /lea\s+(esp,\[esp|ebx,\[ebx|ecx,\[ecx)(\+0+h)?\]$/.test(l)) continue;
    out.push(l);
  }
  return out;
};
if (args[0] === '--list') {
  const lo = parseInt(args[1], 16), hi = parseInt(args[2], 16);
  starts.forEach(([a], idx) => {
    if (a < lo || a > hi) return;
    const b = body(idx);
    const strs = [...new Set(b.map(l => (/; (".*")$/.exec(l) || [])[1]).filter(Boolean))].slice(0, 3).join(' ');
    const callees = [...new Set(b.map(l => (/call\s+([0-9A-F]{8})$/.exec(l) || [])[1]).filter(Boolean))].map(c => c.replace(/^00/, '')).join(',');
    console.log(`${a.toString(16)} ${String(b.length).padStart(5)}  ${strs.slice(0, 110)}  -> ${callees.slice(0, 120)}`);
  });
} else for (const a of args) {
  const idx = starts.findIndex(([s]) => s === parseInt(a, 16));
  if (idx < 0) { console.log(`no function at ${a}`); continue; }
  console.log(`sub_${a}:`);
  console.log(body(idx).join('\n'));
}
