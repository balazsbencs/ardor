/* Ardor Manager mockups: markup builders. Every builder returns an HTML
   string; the pages compose them and the interaction layer delegates
   events from data-act, data-sort-* and .scale attributes. */
(function () {
  "use strict";
  const A = window.Ardor;
  const { DEFS, mainFor } = A.D;
  const esc = A.esc;

  /* ---------- block card ---------- */
  A.card = (preset, b, o = {}) => {
    const def = DEFS[b.def];
    const scene = o.scene ?? null;
    const on = A.isOn(preset, b, scene);
    const sel = o.sel === b.uid;
    const onOv = A.hasOv(preset, b.uid, "on", scene);
    let body = "";
    if (def.eq) {
      body = `<div class="eq" style="height:64px;margin:0 12px 14px">${A.eqSvg(b, { w: 150, h: 64 })}</div>`;
    } else if (def.id === "nam") {
      body = `<div class="blk__params"><div class="blk__p">Source <b>${A.fmtText(def.params[0], b.v.inputMode)}</b></div><div class="blk__p">Nano <b>${b.v.useNano ? "On" : "Off"}</b></div></div>`;
    } else {
      const rows = (o.all ? def.params.map((p) => p.key) : mainFor(def)).map((k) => {
        const p = A.param(b.def, k);
        if (!p) return "";
        const v = A.val(preset, b, k, scene);
        const ov = A.hasOv(preset, b.uid, k, scene);
        return `<div class="blk__p">${esc(p.label)} <b class="${ov ? "ov" : ""}" data-v="${b.uid}:${k}">${A.fmtText(p, v)}</b></div><div class="bar"><i data-bar="${b.uid}:${k}" style="width:${(A.norm(p, v) * 100).toFixed(1)}%"></i></div>`;
      }).join("");
      body = `<div class="blk__params">${rows}</div>`;
    }
    const kind = A.D.ASSET_KIND[b.def];
    const missing = kind && b.asset && !A.get().assets[kind].some((f) => f.filename === b.asset);
    const sub = missing ? '<span class="tag tag--warn">FILE MISSING</span>' : b.asset ? esc(def.name) : kind ? "No file" : "";
    return `<div class="blk ${A.famClass(b)} ${on ? "is-on" : "is-off"} ${sel ? "is-sel" : ""} ${o.cls || ""}"
      tabindex="0" aria-roledescription="block" data-act="select" data-uid="${b.uid}" data-fk="card-${b.uid}"
      data-sort-item data-sort-handle=".blk__cap" aria-current="${sel}"
      aria-label="${esc(A.blockName(b))}, ${esc(def.cap)}, ${on ? "on" : "bypassed"}. Drag the cap to move.">
      <div class="blk__cap"><span>${esc(def.cap)}${onOv ? ' <span class="tag--scene tag">SCENE</span>' : ""}</span><span class="grip">${A.icon("grip")}</span></div>
      <div class="blk__name">${esc(A.blockName(b))}</div>
      ${sub ? `<div class="blk__sub">${sub}</div>` : ""}
      ${o.pow === false ? "" : `<button class="blk__pow" data-act="toggle" data-uid="${b.uid}" aria-label="${on ? "Bypass" : "Turn on"} ${esc(A.blockName(b))}" title="${on ? "Bypass" : "Turn on"} (B)">${A.icon("power")}</button>`}
      ${on ? body : '<span class="offtag">OFF</span>'}
    </div>`;
  };

  /* ---------- chain: jacks, insert points, cards, Dual Rig lanes ---------- */
  const ins = (listId, at, o) => (o.insert === false ? "" : `<button class="ins" data-act="insert" data-list="${listId}" data-at="${at}" aria-label="Add a block here" title="Add a block">${A.icon("plus")}</button>`);
  const listHtml = (preset, list, listId, o) => {
    const parts = list.map((b, i) => `${ins(listId, i, o)}${b.lanes ? A.rig(preset, b, o) : A.card(preset, b, o)}`);
    return `${parts.join("")}${ins(listId, list.length, o)}`;
  };
  A.rig = (preset, b, o) => {
    const on = A.isOn(preset, b, o.scene);
    const lane = (id, name) => {
      const list = b.lanes[id];
      return `<div class="rig__lane" data-lane="${id}"><span class="rig__lane-tag">${name}</span>
        <div class="chain__list" data-sort-list="${b.uid}:${id}" data-axis="x">${list.length ? listHtml(preset, list, `${b.uid}:${id}`, o) : `${ins(`${b.uid}:${id}`, 0, o)}<span class="rig__empty">Empty lane. Drop a block here.</span>`}</div></div>`;
    };
    return `<div class="rig ${on ? "" : "is-off"}" data-sort-item data-uid="${b.uid}" data-sort-handle=".rig__node--split">
      <button class="rig__node rig__node--split" data-act="select" data-uid="${b.uid}" data-fk="card-${b.uid}" aria-label="Dual Rig split. Drag to move.">${A.icon("split")}<b>SPLIT</b>DUAL RIG</button>
      <div class="rig__lanes">${lane("a", "A")}${lane("b", "B")}</div>
      <div class="rig__node">JOIN<b>L / R</b></div>
    </div>`;
  };
  A.chain = (preset, o = {}) => `<div class="chain ${o.cls || ""}">
      ${o.jacks === false ? "" : '<div class="jack">IN<small>MONO</small></div><span class="chain__wire"></span>'}
      <div class="chain__list" data-sort-list="main" data-axis="x">${listHtml(preset, preset.blocks, "main", o)}</div>
      ${o.jacks === false ? "" : '<span class="chain__wire"></span><div class="jack">OUT<small>STEREO</small></div>'}
    </div>`;

  /* ---------- chip strip: the device parameter-screen header ---------- */
  A.chip = (preset, b, o = {}) => {
    const def = DEFS[b.def];
    const on = A.isOn(preset, b, o.scene ?? null);
    const sel = o.sel === b.uid;
    return `<button class="chip ${A.famClass(b)} ${on ? "" : "is-off"} ${sel ? "is-sel" : ""}" data-act="select" data-uid="${b.uid}" data-fk="chip-${b.uid}"
      data-sort-item aria-current="${sel}" style="view-transition-name:b-${b.uid}" title="${esc(A.blockName(b))}. Drag to move.">
      <small>${esc(def.cap)}${on ? "" : " · off"}</small><b>${esc(A.blockName(b))}</b></button>`;
  };
  A.chips = (preset, o = {}) => {
    const item = (b) => (b.lanes
      ? `<div class="chip-rig" data-sort-item data-uid="${b.uid}" data-sort-handle=".chip-rig__head"><button class="chip-rig__head" data-act="select" data-uid="${b.uid}">${A.icon("split")}RIG</button>
          <div class="chip-rig__lanes">${["a", "b"].map((l) => `<div class="chips__list chips__list--lane" data-sort-list="${b.uid}:${l}" data-axis="x"><span class="chip-lane">${l.toUpperCase()}</span>${b.lanes[l].map(item).join("")}</div>`).join("")}</div></div>`
      : A.chip(preset, b, o));
    return `<div class="chips ${o.cls || ""}">${o.jacks === false ? "" : '<span class="chip-jack">IN</span>'}
      <div class="chips__list" data-sort-list="main" data-axis="x">${preset.blocks.map(item).join("")}</div>
      ${o.add === false ? "" : `<button class="chip-add" data-act="insert" data-list="main" data-at="${preset.blocks.length}" aria-label="Add a block at the end">${A.icon("plus")}</button>`}
      ${o.jacks === false ? "" : '<span class="chip-jack">OUT</span>'}</div>`;
  };

  /* ---------- chain strip: family codes, as on the device preset tiles ---------- */
  A.strip = (preset, o = {}) => {
    const seg = (b) => {
      const def = DEFS[b.def];
      if (b.lanes) return `<span class="f-amp w2">RIG ${b.lanes.a.length}+${b.lanes.b.length}</span>`;
      const on = A.isOn(preset, b, o.scene);
      return `<span class="f-${def.fam}${on ? "" : " off"}${def.fam === "amp" || def.fam === "cab" ? " w2" : ""}">${esc(A.blockCode(b))}</span>`;
    };
    return `<div class="strip">${preset.blocks.map(seg).join("")}</div>`;
  };

  /* ---------- preset tile ---------- */
  A.tile = (id, o = {}) => {
    const s = A.get();
    const p = id ? s.presets[id] : null;
    const fs = o.fs ? `<span class="tile__fs">${esc(o.fs)}</span>` : "";
    if (!p) {
      return `<button class="tile is-empty ${o.cls || ""}" data-act="new-preset" data-bank="${o.bank ?? ""}" data-slot="${o.slot ?? ""}" ${o.sortable ? "data-sort-item" : ""}>
        <div class="tile__top">${fs}</div><div class="tile__name">Empty slot</div></button>`;
    }
    const live = s.live.presetId === id;
    const editing = s.editId === id;
    const dirty = A.isDirty(id);
    return `<button class="tile ${live ? "is-live" : ""} ${editing ? "is-edit" : ""} ${o.cls || ""}" data-act="${o.act || "preset"}" data-id="${id}" data-fk="tile-${id}"
      ${o.sortable ? `data-sort-item data-uid="${id}"` : ""} aria-pressed="${editing}" aria-label="${esc(p.name)}${live ? ", live on the pedal" : ""}${editing ? ", open in the editor" : ""}">
      <div class="tile__top">${fs}<span>${live ? '<span class="tag tag--ink">LIVE</span>' : ""}${dirty ? ' <span class="tag tag--warn">EDITED</span>' : ""}${A.missingIn(p).length ? ' <span class="tag tag--warn" title="Uses a file that is not on the pedal">MISSING FILE</span>' : ""}</span></div>
      <div class="tile__name">${esc(p.name)}</div>
      ${o.strip === false ? "" : A.strip(p, { scene: live ? s.live.sceneId : null })}
    </button>`;
  };

  /* ---------- parameter controls ---------- */
  A.control = (preset, b, p, o = {}) => {
    const scene = o.scene ?? null;
    const v = A.val(preset, b, p.key, scene);
    const f = A.fmt(p, v);
    const ov = A.hasOv(preset, b.uid, p.key, scene);
    const k = `${b.uid}:${p.key}`;
    const fk = `ctl-${k}`;
    if (p.kind === "choice") {
      return `<div class="ctl ${A.famClass(b)} ${o.cls || ""}"><div class="ctl__top"><span class="ctl__lbl">${esc(p.label)}</span></div>
        <div class="choice">${p.choices.map(([cv, cl]) => `<button data-act="choice" data-uid="${b.uid}" data-key="${p.key}" data-value="${cv}" aria-pressed="${cv === v}">${esc(cl)}</button>`).join("")}</div></div>`;
    }
    if (p.kind === "toggle") {
      return `<div class="ctl ${A.famClass(b)} ${o.cls || ""}"><div class="ctl__top"><span class="ctl__lbl">${esc(p.label)}</span></div>
        <div class="choice"><button data-act="choice" data-uid="${b.uid}" data-key="${p.key}" data-value="false" aria-pressed="${!v}">Off</button><button data-act="choice" data-uid="${b.uid}" data-key="${p.key}" data-value="true" aria-pressed="${!!v}">On</button></div></div>`;
    }
    const n = A.norm(p, v) * 100;
    return `<div class="ctl ${A.famClass(b)} ${o.compact ? "ctl--compact" : ""} ${o.cls || ""}" data-ctl="${k}">
      <div class="ctl__top"><span class="ctl__lbl">${esc(p.label)}</span>${ov ? `<button class="reset" data-act="clear-ov" data-uid="${b.uid}" data-key="${p.key}" title="Use the preset value in this scene">SCENE ×</button>` : ""}</div>
      <div class="ctl__val"><span data-v="${k}" data-vn>${esc(f.n)}</span><small data-vu="${k}">${esc(f.u)}</small></div>
      <div class="scale" role="slider" tabindex="0" data-fk="${fk}" aria-label="${esc(p.label)}" aria-valuemin="${p.min}" aria-valuemax="${p.max}" aria-valuenow="${v}" aria-valuetext="${esc(A.fmtText(p, v))}"
        data-uid="${b.uid}" data-key="${p.key}" data-def="${b.def}">
        <div class="scale__ticks"></div><div class="scale__track"><div class="scale__fill" style="width:${n.toFixed(2)}%"></div><div class="scale__thumb" style="left:${n.toFixed(2)}%"></div></div>
      </div>
    </div>`;
  };

  // File picker for NAM, cab and IR reverb blocks. Reads the files on the pedal from state.
  A.assetPicker = (b) => {
    const kind = A.D.ASSET_KIND[b.def];
    const files = A.get().assets[kind];
    const missing = b.asset && !files.some((f) => f.filename === b.asset);
    const label = { models: "NAM model", irs: "Cabinet IR", "reverb-irs": "Reverb IR" }[kind];
    return `<div class="ctl ctl--asset ${A.famClass(b)}" style="grid-column:1/-1"><div class="ctl__top"><span class="ctl__lbl">${label}</span>
        <button class="reset" data-act="goto-assets" data-kind="${kind}" title="Upload, rename or delete files">MANAGE FILES</button></div>
      ${missing ? `<p class="asset-missing">${A.icon("folder")}<span><b>${esc(b.asset)}</b> is not on the pedal. Pick another file, or upload it in Assets.</span></p>` : ""}
      <div class="choice">${files.map((f) => `<button data-act="asset" data-uid="${b.uid}" data-value="${esc(f.filename)}" aria-pressed="${f.filename === b.asset}">${esc(f.filename.replace(/\.(nam|wav)$/i, ""))}</button>`).join("")
        || '<span class="note">No files yet. Use Manage files to upload one.</span>'}</div></div>`;
  };

  // All controls of a block, in catalog order.
  A.controls = (preset, b, o = {}) => {
    const def = DEFS[b.def];
    const parts = [];
    if (A.D.ASSET_KIND[def.id]) parts.push(A.assetPicker(b));
    if (def.eq) parts.push(A.eqEditor(b, o));
    def.params.forEach((p) => parts.push(A.control(preset, b, p, o)));
    return parts.join("");
  };

  A.eqEditor = (b, o = {}) => {
    const s = A.get();
    const selBand = s.eqBand ?? 2;
    const band = b.bands[selBand];
    return `<div class="eq-edit ctl f-util" style="grid-column:1/-1">
      <div class="ctl__top"><span class="ctl__lbl">Response</span><span class="choice">${b.bands.map((_, i) => `<button data-act="eq-band" data-band="${i}" aria-pressed="${i === selBand}">Band ${i + 1}</button>`).join("")}</span></div>
      <div class="eq" style="height:${o.eqH || 150}px">${A.eqSvg(b, { w: 600, h: o.eqH || 150, handles: true, sel: selBand, labels: true })}</div>
      <div class="blk__p" style="padding-top:6px">Band ${selBand + 1} <b>${band.f >= 1000 ? `${(band.f / 1000).toFixed(1)} kHz` : `${band.f} Hz`} · ${band.g > 0 ? "+" : ""}${band.g.toFixed(1)} dB · Q ${band.q.toFixed(1)}</b></div>
    </div>`;
  };
})();
