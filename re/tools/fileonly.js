// Strips the inlined in-memory stream branches from readmesh/writemesh so only
// the FILE* path remains. Pattern: "test R,R / jne MEM ... jmp END / MEM: ... END:".
// Usage: node fileonly.js <annotated.asm> <fromAddr> <toAddr> <reg>
const fs = require('fs');
const [file, from, to, reg = 'esi'] = process.argv.slice(2);
const lines = fs.readFileSync(file, 'latin1').split('\n');
const addrOf = l => { const m = /^\s+([0-9A-F]{8}):/.exec(l); return m ? parseInt(m[1], 16) : null; };
const lo = parseInt(from, 16), hi = parseInt(to, 16);
const sel = lines.filter(l => { const a = addrOf(l); return a !== null && a >= lo && a <= hi; });

const skip = [];
for (let i = 0; i < sel.length - 1; i++) {
  if (!new RegExp(`test\\s+${reg},${reg}$`).test(sel[i])) continue;
  const j = /jne\s+([0-9A-F]{8})/.exec(sel[i + 1]);
  if (!j) continue;
  const mem = parseInt(j[1], 16);
  // last jmp before MEM gives END
  let k = i + 2;
  while (k < sel.length && addrOf(sel[k]) < mem) k++;
  const jm = /jmp\s+([0-9A-F]{8})/.exec(sel[k - 1] || '');
  if (jm && parseInt(jm[1], 16) > mem) skip.push([mem, parseInt(jm[1], 16)]);
}
for (const l of sel) {
  const a = addrOf(l);
  if (skip.some(([s, e]) => a >= s && a < e)) continue;
  if (/lea\s+(esp,\[esp|ebx,\[ebx|ecx,\[ecx)\+?0*h?\]|^\s+\S+: (nop|int\s+3|mov\s+edi,edi)$/.test(l)) continue;
  console.log(l);
}
