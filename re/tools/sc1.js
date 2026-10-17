// Reader/writer for Sculptris .sc1 files, reconstructed from readmesh
// (0x443912) and writemesh (0x4419F0) in Sculptris Alpha 6. See
// ../docs/sc1-format.md for the layout.
//
//   node sc1.js info <file.sc1>...        summary
//   node sc1.js roundtrip <file.sc1>...   read + write, compare bytes
//   node sc1.js obj <file.sc1> <out.obj>  export (mirrored if symmetric)
const fs = require('fs');

class Reader {
  constructor(buf) { this.b = buf; this.p = 0; }
  u8() { return this.b[this.p++]; }
  u16() { const v = this.b.readUInt16LE(this.p); this.p += 2; return v; }
  i32() { const v = this.b.readInt32LE(this.p); this.p += 4; return v; }
  f32() { const v = this.b.readFloatLE(this.p); this.p += 4; return v; }
  bytes(n) { const v = Uint8Array.from(this.b.subarray(this.p, this.p + n)); this.p += n; return v; }
  get eof() { return this.p >= this.b.length; }
  // ReadBufferRLE (0x4418B0): alternating literal/run segments, each led by a
  // u8 count, starting with a literal. A run repeats the last literal value.
  rle(count, elemSize) {
    const out = elemSize === 4 ? new Int32Array(count) : new Uint8Array(count);
    let i = 0, last = 0, literal = true;
    while (i < count && !this.eof) {
      const n = this.u8();
      if (literal) for (let k = 0; k < n; k++) out[i++] = last = elemSize === 4 ? this.i32() : this.u8();
      else for (let k = 0; k < n; k++) out[i++] = last;
      literal = !literal;
    }
    return out;
  }
  i32array(n) { const a = new Int32Array(n); for (let i = 0; i < n; i++) a[i] = this.i32(); return a; }
}

class Writer {
  constructor() { this.chunks = []; }
  raw(buf) { this.chunks.push(Buffer.from(buf.buffer ? Buffer.from(buf.buffer, buf.byteOffset, buf.byteLength) : buf)); }
  u8(v) { this.chunks.push(Buffer.from([v & 0xff])); }
  u16(v) { const b = Buffer.alloc(2); b.writeUInt16LE(v); this.chunks.push(b); }
  i32(v) { const b = Buffer.alloc(4); b.writeInt32LE(v); this.chunks.push(b); }
  f32(v) { const b = Buffer.alloc(4); b.writeFloatLE(v); this.chunks.push(b); }
  // WriteBufferRLE (0x4416E0), a direct port of its state machine so the
  // segmentation matches the original byte for byte. A literal ends before a
  // value equal to its predecessor (which then opens the run), at 255 values,
  // or at the end of the buffer (where the equal value is kept in it).
  rle(a, elemSize) {
    const put = v => (elemSize === 4 ? this.i32(v) : this.u8(v));
    const n = a.length;
    let prev = 0, first = true, run = false, seg = [], i = 0;
    while (i < n) {
      const v = a[i];
      let eq = v === prev;
      if (first) { eq = false; first = false; }
      seg.push(v); i++;
      if (!run) {
        prev = v;
        if (!eq && seg.length !== 255 && i !== n) continue;
        if (eq && i !== n) { seg.pop(); i--; }
        this.u8(seg.length); seg.forEach(put);
        run = true; seg = [];
      } else {
        if (eq && seg.length !== 255 && i !== n) continue;
        if (!eq) { seg.pop(); i--; }
        this.u8(seg.length);
        run = false; seg = [];
      }
    }
  }
  get buffer() { return Buffer.concat(this.chunks); }
}

function readSc1(buf) {
  const r = new Reader(buf);
  // Version is the last magic digit. Files without the magic, or with
  // version < 6, are "old format" and are re-read from offset 0.
  const magic = buf.toString('latin1', 0, 8);
  const m = { version: magic.startsWith('SC1Fv') ? magic.charCodeAt(7) - 48 : 0 };
  m.oldFormat = m.version < 6;
  if (m.version > 8) throw new Error(`unsupported version ${m.version}`);
  if (!m.oldFormat) r.p = 8;
  const nv = r.i32(), nt = r.i32();
  m.symmetry = r.u8();
  m.positions = new Float32Array(nv * 3);
  m.vertexWeight = new Uint8Array(nv); // float at vert+0x18, stored *255
  m.vertexFlags = new Uint8Array(nv);  // byte at vert+0x06, 1 = on symmetry plane
  for (let i = 0; i < nv; i++) {
    for (let k = 0; k < 3; k++) m.positions[i * 3 + k] = r.f32();
    m.vertexWeight[i] = m.oldFormat ? Math.round(r.f32() * 255) : r.u8();
    m.vertexFlags[i] = r.u8();
  }
  m.indices = new Int32Array(nt * 3);
  m.triFlags = new Uint8Array(nt);     // byte at tri+0x04, marks its verts (+7)
  for (let i = 0; i < nt; i++) {
    for (let k = 0; k < 3; k++) m.indices[i * 3 + k] = r.i32();
    if (m.version >= 7) m.triFlags[i] = r.u8();
  }
  if (r.eof) return m;                 // old file without extension block

  const ev = m.extVersion = r.i32();   // 8 when written by Alpha 6
  if (ev >= 3) {
    m.extraA = r.f32();                // global 0x4B0A20
    m.extraVec = [r.f32(), r.f32(), r.f32()]; // global 0x4FD3A0
  }
  if (ev === 6) m.ext6Byte = r.u8();
  if (ev >= 7) {
    m.hasPolygonList = r.u8();         // global 0x4FD0CC
    if (m.hasPolygonList) m.polygonList = r.bytes(r.i32()); // one byte per triangle
  }
  m.hasTexturing = r.u8();             // global 0x4B14DD
  if (m.hasTexturing) m.tex = readTexturing(r, m, ev);
  if (!r.eof) m.trailing = buf.length - r.p;
  return m;
}

function readTexturing(r, m, ev) {
  const t = {};
  const np = r.i32();                  // global 0x4BFF58, patch stride 0x1E0
  t.patches = [];
  for (let i = 0; i < np; i++) {       // vec3 at +0x08, +0x28, +0x34, +0x40, +0x4C
    const p = [];
    for (let k = 0; k < 15; k++) p.push(r.f32());
    t.patches.push(p);
  }
  const ntv = r.i32();                 // global 0x4B8D58, texvert stride 0x28
  t.texverts = [];
  for (let i = 0; i < ntv; i++) {
    if (ev < 6) {
      const uvw = [r.f32(), r.f32(), r.f32()], vec = [r.f32(), r.f32(), r.f32()];
      t.texverts.push({ uvw, vec, vertex: r.i32() });
    } else {
      t.texverts.push({ u: r.f32(), v: r.f32(), packedNormal: r.i32(), vertex: r.i32() });
    }
  }
  const nt = m.triFlags.length;
  t.triTexverts = new Int32Array(nt * 3);
  t.triPatch = new Int32Array(nt);
  for (let i = 0; i < nt; i++) {
    for (let k = 0; k < 3; k++) t.triTexverts[i * 3 + k] = r.i32();
    t.triPatch[i] = ev < 6 ? r.i32() : r.u16();
  }
  const img = () => {
    const w = r.i32(), h = r.i32();
    return { w, h, data: ev < 4 ? r.i32array(w * h) : r.rle(w * h, 4) };
  };
  t.color = img();                     // RGBA bytes; globals 0x4B0B3C/0x4B0B40, buffer 0x4FD120
  t.bump = img();                      // i32 heights, flat = 0x10000000; buffer 0x4B0AE8
  if (ev >= 8) {
    t.materials = [];                  // 16 slots of 0x288 bytes at 0x5174B0
    const n = r.i32();
    for (let i = 0; i < n; i++) {
      const slot = r.i32();
      const nameBytes = r.bytes(256);
      const name = Buffer.from(nameBytes).toString('latin1').replace(/\0.*$/s, '');
      const flags = [r.u8(), r.u8(), r.u8()]; // +0x249, +0x24A, +0x24B
      const w = r.i32(), h = r.i32();
      t.materials.push({ slot, name, nameBytes, flags, w, h, mask: r.rle(w * h, 1) });
    }
  }
  return t;
}

// Writes the Alpha 6 layout (SC1Fv008, extension block v8).
function writeSc1(m) {
  const w = new Writer();
  w.raw(Buffer.from('SC1Fv008', 'latin1'));
  const nv = m.positions.length / 3, nt = m.triFlags.length;
  w.i32(nv); w.i32(nt); w.u8(m.symmetry);
  for (let i = 0; i < nv; i++) {
    for (let k = 0; k < 3; k++) w.f32(m.positions[i * 3 + k]);
    w.u8(m.vertexWeight[i]); w.u8(m.vertexFlags[i]);
  }
  for (let i = 0; i < nt; i++) {
    for (let k = 0; k < 3; k++) w.i32(m.indices[i * 3 + k]);
    w.u8(m.triFlags[i]);
  }
  w.i32(8);
  w.f32(m.extraA ?? 1); (m.extraVec || [0, 0, 0]).forEach(v => w.f32(v));
  w.u8(m.hasPolygonList ? 1 : 0);
  if (m.hasPolygonList) { w.i32(m.polygonList.length); w.raw(m.polygonList); }
  w.u8(m.hasTexturing ? 1 : 0);
  if (m.hasTexturing) {
    const t = m.tex;
    w.i32(t.patches.length); t.patches.forEach(p => p.forEach(v => w.f32(v)));
    w.i32(t.texverts.length);
    for (const tv of t.texverts) { w.f32(tv.u); w.f32(tv.v); w.i32(tv.packedNormal); w.i32(tv.vertex); }
    for (let i = 0; i < nt; i++) { for (let k = 0; k < 3; k++) w.i32(t.triTexverts[i * 3 + k]); w.u16(t.triPatch[i]); }
    for (const im of [t.color, t.bump]) { w.i32(im.w); w.i32(im.h); w.rle(im.data, 4); }
    w.i32(t.materials.length);
    for (const mt of t.materials) {
      w.i32(mt.slot); w.raw(mt.nameBytes); mt.flags.forEach(f => w.u8(f));
      w.i32(mt.w); w.i32(mt.h); w.rle(mt.mask, 1);
    }
  }
  return w.buffer;
}

// OBJ export; with symmetry on, the stored half (x >= 0) is mirrored and
// vertices flagged as lying on the plane are shared.
function toObj(m) {
  const nv = m.positions.length / 3, out = [];
  const mirror = new Int32Array(nv);
  let next = nv;
  for (let i = 0; i < nv; i++) out.push(`v ${m.positions[i * 3]} ${m.positions[i * 3 + 1]} ${m.positions[i * 3 + 2]}`);
  if (m.symmetry) for (let i = 0; i < nv; i++) {
    if (m.vertexFlags[i] & 1) mirror[i] = i;
    else { mirror[i] = next++; out.push(`v ${-m.positions[i * 3]} ${m.positions[i * 3 + 1]} ${m.positions[i * 3 + 2]}`); }
  }
  const idx = m.indices;
  for (let i = 0; i < idx.length; i += 3) {
    out.push(`f ${idx[i] + 1} ${idx[i + 1] + 1} ${idx[i + 2] + 1}`);
    if (m.symmetry) out.push(`f ${mirror[idx[i]] + 1} ${mirror[idx[i + 2]] + 1} ${mirror[idx[i + 1]] + 1}`);
  }
  return out.join('\n') + '\n';
}

module.exports = { readSc1, writeSc1, toObj, Reader, Writer };

if (require.main === module) {
  const [cmd, ...args] = process.argv.slice(2);
  const hist = a => { const h = {}; for (const v of a) h[v] = (h[v] || 0) + 1; return JSON.stringify(h); };
  if (cmd === 'info') for (const f of args) {
    const m = readSc1(fs.readFileSync(f));
    let onPlaneOk = 0, flagged = 0;
    for (let i = 0; i < m.vertexFlags.length; i++) if (m.vertexFlags[i] & 1) { flagged++; if (m.positions[i * 3] === 0) onPlaneOk++; }
    console.log(`${f}\n  v${m.version} verts=${m.vertexFlags.length} tris=${m.triFlags.length} symmetry=${m.symmetry} ext=v${m.extVersion} extraA=${m.extraA} texturing=${m.hasTexturing ?? '-'} trailing=${m.trailing || 0}`);
    console.log(`  vertexWeight ${hist(m.vertexWeight).slice(0, 80)}  triFlags ${hist(m.triFlags)}  plane-flagged ${flagged} (x==0: ${onPlaneOk})`);
  }
  else if (cmd === 'roundtrip') for (const f of args) {
    const a = fs.readFileSync(f), m = readSc1(a), b = writeSc1(m);
    const same = a.length === b.length && a.equals(b);
    let first = -1; if (!same) for (let i = 0; i < Math.min(a.length, b.length); i++) if (a[i] !== b[i]) { first = i; break; }
    console.log(`${same ? 'IDENTICAL' : 'DIFFERENT'} ${f} (${a.length} -> ${b.length}${first >= 0 ? `, first diff @0x${first.toString(16)}` : ''})`);
  }
  else if (cmd === 'obj') fs.writeFileSync(args[1], toObj(readSc1(fs.readFileSync(args[0]))));
  else console.log('usage: node sc1.js info|roundtrip|obj ...');
}
