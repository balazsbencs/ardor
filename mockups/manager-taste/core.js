/* Ardor Manager mockups: store, immutable model operations, formatting
   and render helpers shared by all five directions. */
(function () {
  "use strict";
  const D = window.ArdorData;
  const A = (window.Ardor = {});
  A.DEFS = D.DEFS;

  /* ---------- store with undo ---------- */
  let state = D.initial();
  const past = [];
  const future = [];
  const subs = [];
  const emit = () => subs.forEach((fn) => fn(state));
  const markDirty = (s) => (s.dirty.includes(s.editId) ? s : { ...s, dirty: [...s.dirty, s.editId] });

  A.get = () => state;
  A.subscribe = (fn) => { subs.push(fn); };
  A.set = (fn, opts = {}) => {
    const { record = true, dirty = true } = opts;
    const next = fn(state);
    if (!next || next === state) return;
    if (record) { past.push(state); if (past.length > 200) past.shift(); future.length = 0; }
    state = dirty ? markDirty(next) : next;
    emit();
  };
  A.view = (patch) => A.set((s) => ({ ...s, ...patch }), { record: false, dirty: false });
  A.canUndo = () => past.length > 0;
  A.canRedo = () => future.length > 0;
  A.undo = () => { if (!past.length) return; future.push(state); state = past.pop(); emit(); };
  A.redo = () => { if (!future.length) return; past.push(state); state = future.pop(); emit(); };
  A.cur = () => state.presets[state.editId];
  A.isDirty = (id = state.editId) => state.dirty.includes(id);

  /* ---------- pure model helpers ---------- */
  function walk(blocks, listId, fn) {
    blocks.forEach((b, i) => {
      fn(b, listId, i);
      if (b.lanes) { walk(b.lanes.a, `${b.uid}:a`, fn); walk(b.lanes.b, `${b.uid}:b`, fn); }
    });
  }
  A.locate = (preset, uid) => {
    if (uid === "__global") return { block: A.globalBlock(preset), listId: null, index: -1 };
    let hit = null;
    walk(preset.blocks, "main", (b, listId, index) => { if (b.uid === uid) hit = { block: b, listId, index }; });
    return hit;
  };
  A.allBlocks = (preset) => { const out = []; walk(preset.blocks, "main", (b) => out.push(b)); return out; };
  A.getList = (preset, listId) => {
    if (listId === "main") return preset.blocks;
    const [uid, lane] = listId.split(":");
    const rig = A.locate(preset, uid);
    return rig ? rig.block.lanes[lane] : [];
  };
  function mapLists(blocks, listId, target, fn) {
    const mapped = listId === target ? fn(blocks) : blocks;
    return mapped.map((b) => (b.lanes ? {
      ...b,
      lanes: { a: mapLists(b.lanes.a, `${b.uid}:a`, target, fn), b: mapLists(b.lanes.b, `${b.uid}:b`, target, fn) },
    } : b));
  }
  const withList = (preset, listId, fn) => ({ ...preset, blocks: mapLists(preset.blocks, "main", listId, fn) });
  A.mapBlock = (preset, uid, fn) => {
    const hit = A.locate(preset, uid);
    if (!hit) return preset;
    return withList(preset, hit.listId, (list) => list.map((b) => (b.uid === uid ? fn(b) : b)));
  };
  A.removeFrom = (preset, uid) => {
    const hit = A.locate(preset, uid);
    return hit ? withList(preset, hit.listId, (list) => list.filter((b) => b.uid !== uid)) : preset;
  };
  A.insertAt = (preset, listId, index, block) => withList(preset, listId, (list) => [...list.slice(0, index), block, ...list.slice(index)]);
  A.moveTo = (preset, uid, listId, index) => {
    const hit = A.locate(preset, uid);
    if (!hit) return preset;
    if (hit.block.lanes && listId !== "main") return preset;
    const without = A.removeFrom(preset, uid);
    const adjusted = hit.listId === listId && index > hit.index ? index - 1 : index;
    return A.insertAt(without, listId, adjusted, hit.block);
  };
  // Catalog rule: one enabled block per constraint group in a chain.
  A.enforceGroup = (preset, uid) => {
    const hit = A.locate(preset, uid);
    const group = hit && D.DEFS[hit.block.def].group;
    if (!group) return { preset, off: [] };
    const off = [];
    const next = withList(preset, hit.listId, (list) => list.map((b) => {
      if (b.uid === uid || !b.on || D.DEFS[b.def].group !== group) return b;
      off.push(D.DEFS[b.def].name);
      return { ...b, on: false };
    }));
    return { preset: next, off };
  };

  /* ---------- scenes ---------- */
  A.sceneOf = (preset, sceneId) => (preset.scenes && sceneId ? preset.scenes.find((s) => s.id === sceneId) : null);
  A.val = (preset, block, key, sceneId) => {
    const sc = A.sceneOf(preset, sceneId);
    const k = `${block.uid}:${key}`;
    return sc && k in sc.ov ? sc.ov[k] : block.v[key];
  };
  A.isOn = (preset, block, sceneId) => {
    const sc = A.sceneOf(preset, sceneId);
    const k = `${block.uid}:on`;
    return sc && k in sc.ov ? sc.ov[k] : block.on;
  };
  A.hasOv = (preset, uid, key, sceneId) => { const sc = A.sceneOf(preset, sceneId); return !!sc && `${uid}:${key}` in sc.ov; };
  A.ovCount = (preset, sceneId) => { const sc = A.sceneOf(preset, sceneId); return sc ? Object.keys(sc.ov).length : 0; };
  const setOv = (preset, sceneId, k, value) => ({
    ...preset,
    scenes: preset.scenes.map((s) => {
      if (s.id !== sceneId) return s;
      const ov = { ...s.ov };
      if (value === undefined) delete ov[k]; else ov[k] = value;
      return { ...s, ov };
    }),
  });

  /* ---------- edit actions (scope: base preset or the scene in edit) ---------- */
  const edit = (fn, opts) => A.set((s) => {
    const next = fn(s.presets[s.editId], s);
    return next ? { ...s, presets: { ...s.presets, [s.editId]: next } } : s;
  }, opts);
  A.edit = edit;
  A.setParam = (uid, key, value, opts) => edit((p, s) => {
    if (s.scene && p.scenes) return setOv(p, s.scene, `${uid}:${key}`, value);
    if (uid === "__global") return { ...p, [key]: value };
    return A.mapBlock(p, uid, (b) => ({ ...b, v: { ...b.v, [key]: value } }));
  }, opts);
  // Input and output as a pseudo block, so the same controls and scene overrides work.
  A.globalBlock = (p) => ({ uid: "__global", def: "global", on: true, v: { inputDb: p.inputDb, outputDb: p.outputDb } });
  A.clearOv = (uid, key) => edit((p, s) => (s.scene ? setOv(p, s.scene, `${uid}:${key}`, undefined) : null));
  A.setAsset = (uid, asset) => edit((p) => A.mapBlock(p, uid, (b) => ({ ...b, asset })));
  A.toggle = (uid) => {
    const s = state;
    const p = A.cur();
    const hit = A.locate(p, uid);
    if (!hit) return;
    if (s.scene && p.scenes) {
      edit((pp) => setOv(pp, s.scene, `${uid}:on`, !A.isOn(pp, hit.block, s.scene)));
      return;
    }
    let off = [];
    edit((pp) => {
      const flipped = A.mapBlock(pp, uid, (b) => ({ ...b, on: !b.on }));
      if (hit.block.on) return flipped;
      const res = A.enforceGroup(flipped, uid);
      off = res.off;
      return res.preset;
    });
    if (off.length) A.toast(`${off.join(", ")} turned off. One block of this type runs at a time.`);
  };
  A.move = (uid, listId, index) => edit((p) => A.moveTo(p, uid, listId, index));
  A.remove = (uid) => {
    const p = A.cur();
    const hit = A.locate(p, uid);
    if (!hit) return;
    edit((pp) => A.removeFrom(pp, uid));
    if (state.sel === uid) A.view({ sel: null });
    A.toast(`${A.blockName(hit.block)} removed. Undo with ⌘Z.`);
  };
  A.duplicate = (uid) => {
    const p = A.cur();
    const hit = A.locate(p, uid);
    if (!hit || hit.block.lanes) return;
    const copy = { ...structuredClone(hit.block), uid: `${hit.block.uid}-c${Date.now() % 10000}`, on: D.DEFS[hit.block.def].group ? false : hit.block.on };
    edit((pp) => A.insertAt(pp, hit.listId, hit.index + 1, copy));
    A.view({ sel: copy.uid });
  };
  A.add = (defId, listId, index) => {
    const kind = D.ASSET_KIND[defId];
    const b = D.newBlock(defId, true, {}, kind && state.assets[kind][0] ? { asset: state.assets[kind][0].filename } : {});
    if (defId === "dualRig") b.lanes = { a: [], b: [] };
    let off = [];
    edit((p) => {
      const inserted = A.insertAt(p, listId, index, b);
      const res = A.enforceGroup(inserted, b.uid);
      off = res.off;
      return res.preset;
    });
    A.view({ sel: b.uid });
    if (off.length) A.toast(`${DEFS_NAME(defId)} added. ${off.join(", ")} turned off: one block of this type runs at a time.`);
    return off;
  };
  const DEFS_NAME = (id) => D.DEFS[id].name;
  A.selectPreset = (id) => {
    if (!state.presets[id]) return;
    A.view({ editId: id, sel: null, scene: null });
  };
  A.save = () => {
    if (!A.isDirty()) { A.toast("No changes to save"); return; }
    A.set((s) => ({ ...s, dirty: s.dirty.filter((d) => d !== s.editId) }), { record: false, dirty: false });
    A.toast(`Saved to bank ${A.slotOf(state.editId).label}`);
  };
  A.makeLive = (id = state.editId, sceneId) => {
    const p = state.presets[id];
    A.view({ live: { presetId: id, sceneId: sceneId || (p.scenes ? p.scenes[0].id : null) } });
    A.toast(`${p.name} is live on the pedal`);
  };
  A.slotOf = (id) => {
    for (const bank of state.banks) {
      const i = bank.slots.indexOf(id);
      if (i >= 0) return { bank: bank.n, slot: i + 1, bankName: bank.name, label: `${String(bank.n).padStart(2, "0")} · slot ${i + 1}` };
    }
    return { bank: 0, slot: 0, label: "" };
  };

  /* ---------- formatting ---------- */
  const round = (v, d = 0) => { const m = 10 ** d; return Math.round(v * m) / m; };
  A.fmt = (p, v) => {
    if (p.kind === "choice") { const c = p.choices.find((x) => x[0] === v); return { n: c ? c[1] : v, u: "" }; }
    if (p.kind === "toggle") return { n: v ? "On" : "Off", u: "" };
    switch (p.unit) {
      case "percent": return { n: String(Math.round(v * 100)), u: "%" };
      case "db": { const r = Math.abs(v) < 10 && !Number.isInteger(v) ? round(v, 1) : Math.round(v); return { n: `${r > 0 ? "+" : ""}${r}`, u: "dB" }; }
      case "ms": return { n: String(v < 10 ? round(v, 1) : Math.round(v)), u: "ms" };
      case "hz": return v <= 0 ? { n: "Off", u: "" } : v >= 1000 ? { n: String(round(v / 1000, 1)), u: "kHz" } : { n: String(Math.round(v)), u: "Hz" };
      case "ratio": return { n: `${round(v, 1)}:1`, u: "" };
      case "x": return { n: round(v, 2).toFixed(2), u: "×" };
      case "delayTime": return { n: String(Math.round(60 + 2440 * v * v)), u: "ms" };
      default: return { n: String(round(v, 2)), u: "" };
    }
  };
  A.fmtText = (p, v) => { const f = A.fmt(p, v); return f.u ? `${f.n} ${f.u}` : f.n; };
  A.norm = (p, v) => (p.max === p.min ? 0 : Math.min(1, Math.max(0, (v - p.min) / (p.max - p.min))));
  A.param = (defId, key) => D.DEFS[defId].params.find((p) => p.key === key);
  A.blockName = (b) => {
    const def = D.DEFS[b.def];
    if (b.asset) return b.asset.replace(/\.(nam|wav)$/i, "");
    return def.name;
  };
  A.blockCode = (b) => {
    const def = D.DEFS[b.def];
    if (def.id === "nam" && b.asset) return b.asset.split(" ")[0].slice(0, 5).toUpperCase();
    if (def.id === "cab" && b.asset) return A.blockName(b).split(" ").find((w) => /\dx\d/i.test(w))?.toUpperCase() || "CAB";
    return def.code;
  };
  A.esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

  /* ---------- icons (authored, 24 px grid, one stroke weight) ---------- */
  const ICONS = {
    grip: '<circle cx="9" cy="6" r="1"/><circle cx="15" cy="6" r="1"/><circle cx="9" cy="12" r="1"/><circle cx="15" cy="12" r="1"/><circle cx="9" cy="18" r="1"/><circle cx="15" cy="18" r="1"/>',
    plus: '<path d="M12 5v14M5 12h14"/>',
    power: '<path d="M12 3v8"/><path d="M7 6.3a7.5 7.5 0 1 0 10 0"/>',
    undo: '<path d="M9 14 4 9l5-5"/><path d="M4 9h10.5a5.5 5.5 0 0 1 0 11H11"/>',
    redo: '<path d="m15 14 5-5-5-5"/><path d="M20 9H9.5a5.5 5.5 0 0 0 0 11H13"/>',
    save: '<path d="M5 3h11l3 3v15H5z"/><path d="M8 3v5h7V3"/><path d="M8 21v-7h8v7"/>',
    send: '<path d="M21 3 10 14"/><path d="m21 3-7 18-4-7-7-4z"/>',
    search: '<circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/>',
    close: '<path d="M18 6 6 18M6 6l12 12"/>',
    copy: '<rect x="8" y="8" width="13" height="13"/><path d="M4 16V4h12"/>',
    trash: '<path d="M3 6h18"/><path d="M8 6V3h8v3"/><path d="m6 6 1 15h10l1-15"/>',
    left: '<path d="m15 18-6-6 6-6"/>',
    right: '<path d="m9 18 6-6-6-6"/>',
    down: '<path d="m6 9 6 6 6-6"/>',
    up: '<path d="m6 15 6-6 6 6"/>',
    sliders: '<path d="M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3M1 14h6M9 8h6M17 16h6"/>',
    folder: '<path d="M3 6h6l2 2h10v11H3z"/>',
    grid: '<rect x="3" y="3" width="7" height="7"/><rect x="14" y="3" width="7" height="7"/><rect x="3" y="14" width="7" height="7"/><rect x="14" y="14" width="7" height="7"/>',
    cmd: '<path d="M9 6a3 3 0 1 0-3 3h12a3 3 0 1 0-3-3v12a3 3 0 1 0 3-3H6a3 3 0 1 0 3 3z"/>',
    check: '<path d="M20 6 9 17l-5-5"/>',
    pedal: '<path d="M4 20h16"/><path d="M7 20 9 8l9-3-1 15"/>',
    midi: '<circle cx="12" cy="12" r="9"/><circle cx="8" cy="11" r=".6"/><circle cx="16" cy="11" r=".6"/><circle cx="12" cy="8" r=".6"/><circle cx="9.5" cy="15" r=".6"/><circle cx="14.5" cy="15" r=".6"/>',
    split: '<path d="M4 12h5l4-6h7M13 18h7M9 12l4 6"/>',
    ab: '<rect x="3" y="5" width="18" height="14"/><path d="M12 5v14"/>',
    menu: '<path d="M4 7h16M4 12h16M4 17h16"/>',
    gear: '<circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M2 12h3M19 12h3M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1"/>',
    move: '<path d="M5 9 2 12l3 3M19 9l3 3-3 3M2 12h20"/>',
    reset: '<path d="M3 12a9 9 0 1 0 3-6.7L3 8"/><path d="M3 3v5h5"/>',
    list: '<path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01"/>',
    pencil: '<path d="M4 20h4L19 9l-4-4L4 16z"/><path d="m13.5 6.5 4 4"/>',
    upload: '<path d="M12 15V3"/><path d="m7 8 5-5 5 5"/><path d="M4 21h16"/>',
    alert: '<path d="M12 3 2 20h20z"/><path d="M12 10v4M12 17h.01"/>',
  };
  A.icon = (name, cls = "") => `<svg class="ic ${cls}" viewBox="0 0 24 24" aria-hidden="true">${ICONS[name] || ""}</svg>`;

  /* ---------- EQ response (display approximation of five peaking bands) ---------- */
  A.eqPath = (bands, w, h, range = 18) => {
    const pts = [];
    for (let i = 0; i <= 96; i++) {
      const f = 20 * Math.pow(1000, i / 96);
      const g = bands.reduce((sum, b) => sum + b.g * Math.exp(-Math.pow(Math.log2(f / b.f), 2) * b.q * 1.6), 0);
      pts.push([(i / 96) * w, h / 2 - (g / range) * (h / 2)]);
    }
    return pts.map((p, i) => `${i ? "L" : "M"}${p[0].toFixed(1)} ${p[1].toFixed(1)}`).join(" ");
  };
  A.eqX = (f, w) => (Math.log(f / 20) / Math.log(1000)) * w;
  A.eqSvg = (block, { w = 300, h = 120, handles = false, sel = -1, labels = false } = {}) => {
    const d = A.eqPath(block.bands, w, h);
    const grid = [100, 1000, 10000].map((f) => `<line class="grid" x1="${A.eqX(f, w)}" x2="${A.eqX(f, w)}" y1="0" y2="${h}"/>`).join("");
    const lbl = labels ? [[100, "100"], [1000, "1K"], [10000, "10K"]].map(([f, t]) => `<text x="${A.eqX(f, w) + 4}" y="${h - 6}">${t}</text>`).join("") : "";
    const dots = handles ? block.bands.map((b, i) => `<circle class="band${i === sel ? " is-sel" : ""}" data-band="${i}" cx="${A.eqX(b.f, w)}" cy="${h / 2 - (b.g / 18) * (h / 2)}" r="7"/>`).join("") : "";
    return `<svg viewBox="0 0 ${w} ${h}" preserveAspectRatio="none">${grid}<line class="zero" x1="0" x2="${w}" y1="${h / 2}" y2="${h / 2}"/><path class="area" d="${d} L${w} ${h / 2} L0 ${h / 2}Z"/><path class="curve" d="${d}"/>${dots}${lbl}</svg>`;
  };

  /* ---------- assets: usage and missing files ---------- */
  A.assetRefs = (preset) => A.allBlocks(preset).filter((b) => D.ASSET_KIND[b.def] && b.asset).map((b) => ({ kind: D.ASSET_KIND[b.def], file: b.asset, uid: b.uid }));
  A.usage = (kind, filename) => Object.values(state.presets).filter((p) => A.assetRefs(p).some((r) => r.kind === kind && r.file === filename));
  A.missingIn = (preset) => A.assetRefs(preset).filter((r) => !state.assets[r.kind].some((f) => f.filename === r.file));

  A.famClass = (b) => `f-${D.DEFS[b.def].fam}`;
  A.D = D;
})();
