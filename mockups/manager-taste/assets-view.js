/* Ardor Manager mockup 1: the Assets view, in the Stage and Drawer grammar.
   Kind tiles take the place of the bank bar, the file list is the stage,
   and a selected file opens a drawer with its details and actions.

   File operations change files on the pedal (upload, replace, rename,
   delete), so they do not go into the undo history. "Try in preset" is a
   preset edit, so it does. The real API (apps/manager/src/api/client.ts)
   supports upload with replace, rename with preset update, and delete. */
(function () {
  "use strict";
  const A = window.Ardor;
  const esc = A.esc;

  const KINDS = {
    models: { label: "NAM models", one: "NAM model", ext: ".nam", fam: "amp", code: "NAM", block: "NAM Model" },
    irs: { label: "Cabinet IRs", one: "cabinet IR", ext: ".wav", fam: "cab", code: "CAB", block: "Cabinet IR" },
    "reverb-irs": { label: "Reverb IRs", one: "reverb IR", ext: ".wav", fam: "rev", code: "IRV", block: "Convolution Reverb" },
  };
  const v = { kind: "models", query: "", sort: "name", checked: new Set(), open: null, renaming: null, renameError: "", confirm: false, queue: [], drag: false };
  const size = (b) => (b < 1024 * 1024 ? `${Math.max(1, Math.round(b / 1024))} KB` : `${(b / 1024 / 1024).toFixed(1)} MB`);
  const stem = (f) => f.replace(/\.(nam|wav)$/i, "");
  const files = (kind = v.kind) => A.get().assets[kind];
  const setAssets = (kind, fn) => A.set((s) => ({ ...s, assets: { ...s.assets, [kind]: fn(s.assets[kind]) } }), { record: false, dirty: false });
  const fileId = (name) => name.toLowerCase().replace(/[^a-z0-9]+/g, "-");

  /* ---------- file operations ---------- */
  // Route a dropped file by extension: .nam to models; .wav to the open IR kind, else cabinet IRs.
  const routeKind = (name) => {
    const low = name.toLowerCase();
    if (low.endsWith(".nam")) return "models";
    if (low.endsWith(".wav")) return v.kind === "reverb-irs" ? "reverb-irs" : "irs";
    return null;
  };
  let seq = 0;
  function enqueue(list) {
    const items = [...list].map((f) => {
      const kind = routeKind(f.name);
      const exists = kind && files(kind).some((x) => x.filename.toLowerCase() === f.name.toLowerCase());
      return { id: ++seq, name: f.name, bytes: f.size || 400000, kind, state: !kind ? "rejected" : exists ? "conflict" : "uploading", progress: 0 };
    });
    v.queue = [...v.queue, ...items];
    v.open = null;
    v.renaming = null;
    const first = items.find((i) => i.kind);
    if (first && first.kind !== v.kind) v.kind = first.kind;
    items.filter((i) => i.state === "uploading").forEach(run);
    A.repaint();
  }
  function run(item) {
    const tick = () => {
      const cur = v.queue.find((q) => q.id === item.id);
      if (!cur || cur.state !== "uploading") return;
      const progress = Math.min(1, cur.progress + 0.12 + Math.random() * 0.1);
      v.queue = v.queue.map((q) => (q.id === item.id ? { ...q, progress } : q));
      if (progress < 1) { A.repaint(); setTimeout(tick, 90); return; }
      finish(cur);
    };
    setTimeout(tick, 90);
  }
  function finish(item) {
    setAssets(item.kind, (list) => [...list.filter((f) => f.filename.toLowerCase() !== item.name.toLowerCase()), { id: fileId(item.name), filename: item.name, sizeBytes: item.bytes }]);
    v.queue = v.queue.filter((q) => q.id !== item.id);
    A.toast(`${item.name} uploaded to ${KINDS[item.kind].label}`);
  }
  function rename(kind, from, to) {
    const next = to.trim();
    const ext = KINDS[kind].ext;
    if (!next) return "Type a file name.";
    if (!next.toLowerCase().endsWith(ext)) return `The name must end in ${ext}.`;
    if (next !== from && files(kind).some((f) => f.filename.toLowerCase() === next.toLowerCase())) return "A file with that name is already on the pedal.";
    const users = A.usage(kind, from);
    A.set((s) => ({
      ...s,
      assets: { ...s.assets, [kind]: s.assets[kind].map((f) => (f.filename === from ? { ...f, id: fileId(next), filename: next } : f)) },
      presets: Object.fromEntries(Object.entries(s.presets).map(([id, p]) => [id, A.assetRefs(p).filter((r) => r.kind === kind && r.file === from)
        .reduce((acc, r) => A.mapBlock(acc, r.uid, (b) => ({ ...b, asset: next })), p)])),
    }), { record: false, dirty: false });
    v.open = next;
    A.toast(`Renamed to ${next}.${users.length ? ` ${users.length} saved preset${users.length === 1 ? "" : "s"} updated.` : ""}`);
    return "";
  }
  function remove(kind, names) {
    setAssets(kind, (list) => list.filter((f) => !names.includes(f.filename)));
    v.checked = new Set();
    if (names.includes(v.open)) v.open = null;
    v.confirm = false;
    A.toast(names.length === 1 ? `${names[0]} deleted from the pedal` : `${names.length} files deleted from the pedal`);
  }
  // Try a file in the preset open in the editor: a normal, undoable preset edit.
  const tryTarget = (kind) => A.allBlocks(A.cur()).find((b) => A.D.ASSET_KIND[b.def] === kind);

  /* ---------- actions ---------- */
  Object.assign(A.acts, {
    "a-kind": (el) => { v.kind = el.dataset.kind; v.open = null; v.checked = new Set(); v.confirm = false; v.renaming = null; A.repaint(); },
    "a-open": (el) => { v.open = v.open === el.dataset.file ? null : el.dataset.file; v.renaming = null; v.confirm = false; A.repaint(); },
    "a-close": () => { v.open = null; v.renaming = null; A.repaint(); },
    "a-sort": (el) => { v.sort = el.dataset.sort; A.repaint(); },
    "a-check": (el) => { const n = new Set(v.checked); if (!n.delete(el.dataset.file)) n.add(el.dataset.file); v.checked = n; v.confirm = false; A.repaint(); },
    "a-checkall": () => { const vis = visible().map((f) => f.filename); v.checked = vis.every((f) => v.checked.has(f)) ? new Set() : new Set(vis); A.repaint(); },
    "a-upload": () => { const i = document.getElementById("a-file"); i.accept = KINDS[v.kind].ext; i.multiple = true; i.click(); },
    "a-replace": (el) => { const i = document.getElementById("a-file"); i.accept = KINDS[v.kind].ext; i.multiple = false; i.dataset.replace = el.dataset.file; i.click(); },
    "a-conflict": (el) => {
      const id = Number(el.dataset.id);
      const item = v.queue.find((q) => q.id === id);
      if (el.dataset.choice === "replace") { v.queue = v.queue.map((q) => (q.id === id ? { ...q, state: "uploading" } : q)); run(item); }
      else v.queue = v.queue.filter((q) => q.id !== id);
      A.repaint();
    },
    "a-dismiss": (el) => { v.queue = v.queue.filter((q) => q.id !== Number(el.dataset.id)); A.repaint(); },
    "a-rename": (el) => { v.renaming = el.dataset.file; v.open = el.dataset.file; v.renameError = ""; A.repaint(); requestAnimationFrame(() => { const i = document.getElementById("a-rename"); i?.focus(); i?.setSelectionRange(0, stem(el.dataset.file).length); }); },
    "a-rename-save": () => { const err = rename(v.kind, v.renaming, document.getElementById("a-rename").value); v.renameError = err; if (!err) v.renaming = null; A.repaint(); },
    "a-rename-cancel": () => { v.renaming = null; A.repaint(); },
    "a-delete": (el) => { if (el.dataset.file) { v.checked = new Set([el.dataset.file]); } v.confirm = true; A.repaint(); },
    "a-delete-yes": () => remove(v.kind, [...v.checked]),
    "a-delete-no": () => { v.confirm = false; A.repaint(); },
    "a-try": (el) => {
      const b = tryTarget(v.kind);
      if (!b) return;
      const was = b.asset;
      A.setAsset(b.uid, el.dataset.file);
      A.toast(`${A.cur().name} now uses ${stem(el.dataset.file)}${was ? ` instead of ${stem(was)}` : ""}. Undo with ⌘Z.`);
    },
    "a-tone3000": () => A.toast("TONE3000 opens in a new window. Pick a capture there and the pedal installs it."),
    "a-repair": (el) => { A.selectPreset(el.dataset.id); A.goto("edit"); const hit = A.locate(A.cur(), el.dataset.uid); if (hit) A.view({ sel: el.dataset.uid }); },
  });

  document.addEventListener("change", (ev) => {
    if (ev.target.id !== "a-file") return;
    const input = ev.target;
    const list = [...input.files];
    if (input.dataset.replace && list[0]) {
      // Upload with replace keeps the name, so every preset that uses it gets the new file.
      const target = input.dataset.replace;
      v.queue = [...v.queue, { id: ++seq, name: target, bytes: list[0].size, kind: v.kind, state: "uploading", progress: 0 }];
      run(v.queue[v.queue.length - 1]);
      A.toast(`Replacing ${target}. Presets that use it get the new file.`);
    } else enqueue(list);
    delete input.dataset.replace;
    input.value = "";
  });
  document.addEventListener("input", (ev) => { if (ev.target.id === "a-q") { v.query = ev.target.value; A.repaint(); } });
  document.addEventListener("keydown", (ev) => {
    if (ev.target.id === "a-rename") {
      if (ev.key === "Enter") { ev.preventDefault(); A.acts["a-rename-save"](); }
      if (ev.key === "Escape") { ev.preventDefault(); ev.stopPropagation(); A.acts["a-rename-cancel"](); }
    }
  }, true);

  // Drop files anywhere in the window. The Edit view switches to Assets.
  const hasFiles = (ev) => [...(ev.dataTransfer?.types || [])].includes("Files");
  let depth = 0;
  document.addEventListener("dragenter", (ev) => { if (!hasFiles(ev)) return; depth += 1; if (!v.drag) { v.drag = true; A.repaint(); } });
  document.addEventListener("dragleave", () => { depth = Math.max(0, depth - 1); if (!depth && v.drag) { v.drag = false; A.repaint(); } });
  document.addEventListener("dragover", (ev) => { if (hasFiles(ev)) ev.preventDefault(); });
  document.addEventListener("drop", (ev) => {
    if (!hasFiles(ev)) return;
    ev.preventDefault();
    depth = 0; v.drag = false;
    A.goto("assets");
    enqueue(ev.dataTransfer.files);
  });

  /* ---------- views ---------- */
  function visible() {
    const q = v.query.trim().toLowerCase();
    const list = files().filter((f) => !q || f.filename.toLowerCase().includes(q));
    const by = { name: (a, b) => a.filename.localeCompare(b.filename), size: (a, b) => b.sizeBytes - a.sizeBytes, used: (a, b) => A.usage(v.kind, b.filename).length - A.usage(v.kind, a.filename).length || a.filename.localeCompare(b.filename) };
    return [...list].sort(by[v.sort]);
  }

  function bar() {
    const s = A.get();
    const total = Object.values(s.assets).flat();
    const missing = Object.values(s.presets).flatMap((p) => A.missingIn(p));
    return `<div class="akinds">${Object.entries(KINDS).map(([k, m]) => {
      const list = s.assets[k];
      return `<button class="akind f-${m.fam}" data-act="a-kind" data-kind="${k}" aria-pressed="${v.kind === k}">
        <span class="akind__code">${m.code}</span><span class="akind__txt"><b>${m.label}</b><small>${m.ext} · for ${m.block}</small></span>
        <span class="akind__n">${list.length}<small>${size(list.reduce((n, f) => n + f.sizeBytes, 0))}</small></span></button>`;
    }).join("")}</div>
      <div class="asum"><b>${total.length} files on the pedal</b><span>${size(total.reduce((n, f) => n + f.sizeBytes, 0))}${missing.length ? ` · <em>${missing.length} missing</em>` : ""}</span></div>`;
  }

  function queueRows() {
    return v.queue.filter((q) => q.kind === v.kind || q.state === "rejected").map((q) => {
      if (q.state === "rejected") return `<div class="aq aq--bad" role="alert">${A.icon("close")}<span><b>${esc(q.name)}</b> is not a .nam or .wav file. The pedal takes NAM models and WAV impulse responses.</span><button class="btn btn--sm btn--quiet" data-act="a-dismiss" data-id="${q.id}">Dismiss</button></div>`;
      if (q.state === "conflict") return `<div class="aq aq--warn" role="alert">${A.icon("copy")}<span><b>${esc(q.name)}</b> is already on the pedal. Replace it? Presets that use it get the new file.</span>
        <button class="btn btn--sm" data-act="a-conflict" data-id="${q.id}" data-choice="skip">Skip</button><button class="btn btn--sm btn--danger" data-act="a-conflict" data-id="${q.id}" data-choice="replace">Replace</button></div>`;
      return `<div class="aq"><span class="aq__name">${A.icon("upload")}<b>${esc(q.name)}</b></span><span class="aq__bar"><i style="transform:scaleX(${q.progress.toFixed(3)})"></i></span><span class="aq__pct">${Math.round(q.progress * 100)}%</span></div>`;
    }).join("");
  }

  function missingNote() {
    const refs = Object.values(A.get().presets).flatMap((p) => A.missingIn(p).filter((r) => r.kind === v.kind).map((r) => ({ ...r, p })));
    if (!refs.length) return "";
    // One compact row per preset. The preset stays saved; the pedal cannot load it until the file is back.
    return refs.map((r) => `<div class="amiss" title="The preset stays saved, but the pedal cannot load it until the file is back or you pick another one.">${A.icon("alert")}
      <span><b>${esc(r.p.name)}</b> needs <b>${esc(r.file)}</b>, which is not on the pedal.</span>
      <button class="btn btn--sm" data-act="a-upload">${A.icon("upload")}Upload</button><button class="btn btn--sm" data-act="a-repair" data-id="${r.p.id}" data-uid="${r.uid}">Pick another</button></div>`).join("");
  }

  function rows() {
    const m = KINDS[v.kind];
    const list = visible();
    if (!files().length) return `<div class="aempty"><span class="akind__code f-${m.fam}">${m.code}</span><h3>No ${m.label.toLowerCase()} yet</h3><p>Drop ${m.ext} files anywhere on this page, or press Upload. Then pick them in a ${m.block} block.</p></div>`;
    if (!list.length) return `<div class="aempty"><h3>No file matches “${esc(v.query)}”</h3><p>Search looks at the file name.</p></div>`;
    const all = list.every((f) => v.checked.has(f.filename));
    return `<div class="arow arow--head"><button class="acheck" data-act="a-checkall" aria-pressed="${all}" aria-label="Select all">${all ? A.icon("check") : ""}</button><span>Name</span><span>Size</span><span>Used in</span><span></span></div>
      ${list.map((f) => {
        const used = A.usage(v.kind, f.filename);
        const on = v.checked.has(f.filename);
        return `<div class="arow f-${m.fam} ${v.open === f.filename ? "is-open" : ""} ${on ? "is-checked" : ""}">
          <button class="acheck" data-act="a-check" data-file="${esc(f.filename)}" aria-pressed="${on}" aria-label="Select ${esc(f.filename)}">${on ? A.icon("check") : ""}</button>
          <button class="aname" data-act="a-open" data-file="${esc(f.filename)}" data-fk="af-${f.id}"><span class="sq"></span><b>${esc(stem(f.filename))}</b><small>${m.ext}</small></button>
          <span class="asize">${size(f.sizeBytes)}</span>
          <span class="aused">${used.length ? used.slice(0, 3).map((p) => `<span class="pchip">${esc(p.name)}</span>`).join("") + (used.length > 3 ? `<span class="pchip pchip--more">+${used.length - 3}</span>` : "") : '<span class="unused">Not used</span>'}</span>
          <span class="aact"><button class="btn btn--sm btn--icon btn--quiet" data-act="a-rename" data-file="${esc(f.filename)}" aria-label="Rename ${esc(f.filename)}" title="Rename">${A.icon("pencil")}</button>
            <button class="btn btn--sm btn--icon btn--quiet" data-act="a-delete" data-file="${esc(f.filename)}" aria-label="Delete ${esc(f.filename)}" title="Delete">${A.icon("trash")}</button></span>
        </div>`;
      }).join("")}`;
  }

  function drawer() {
    const f = files().find((x) => x.filename === v.open);
    if (!f) return "";
    const m = KINDS[v.kind];
    const used = A.usage(v.kind, f.filename);
    const target = tryTarget(v.kind);
    const inUse = target && target.asset === f.filename;
    const renameForm = v.renaming === f.filename ? `<div class="arename"><label class="search"><input id="a-rename" value="${esc(f.filename)}" aria-label="New file name"></label>
      <button class="btn btn--primary" data-act="a-rename-save">Rename</button><button class="btn btn--quiet" data-act="a-rename-cancel">Cancel</button>
      <p class="note">${v.renameError ? `<span class="err">${esc(v.renameError)}</span>` : `Keep the ${m.ext} ending. ${used.length ? `${used.length} saved preset${used.length === 1 ? "" : "s"} will use the new name.` : "No preset uses this file."}`}</p></div>` : "";
    return `<section class="drawer f-${m.fam} adrawer" aria-label="${esc(f.filename)}">
      <div class="drawer__head">
        <span class="tag fam">${esc(m.one)}</span><h2>${esc(stem(f.filename))}</h2><span class="sub">${esc(v.kind)}/${esc(f.filename)} · ${size(f.sizeBytes)}</span>
        <span class="push">
          <button class="btn ${inUse ? "" : "btn--primary"}" data-act="a-try" data-file="${esc(f.filename)}" ${target && !inUse ? "" : "disabled"} title="${target ? "" : `${esc(A.cur().name)} has no ${m.block} block`}">${A.icon("send")}${inUse ? `In ${esc(A.cur().name)}` : `Try in ${esc(A.cur().name)}`}</button>
          <button class="btn" data-act="a-rename" data-file="${esc(f.filename)}">${A.icon("pencil")}Rename</button>
          <button class="btn" data-act="a-replace" data-file="${esc(f.filename)}">${A.icon("upload")}Replace file</button>
          <button class="btn btn--danger" data-act="a-delete" data-file="${esc(f.filename)}">${A.icon("trash")}Delete</button>
          <button class="btn btn--icon btn--quiet" data-act="a-close" aria-label="Close">${A.icon("close")}</button></span>
      </div>
      ${renameForm}
      <div class="ausers"><h3>${used.length ? `Used in ${used.length} preset${used.length === 1 ? "" : "s"}` : "Not used in a preset"}</h3>
        ${used.length ? `<div class="ausers__tiles">${used.map((p) => { const sl = A.slotOf(p.id); return A.tile(p.id, { fs: `Bank ${String(sl.bank).padStart(2, "0")} · FS ${sl.slot}`, act: "a-open-preset" }); }).join("")}</div>`
          : `<p class="note">You can delete it and no preset changes. Or pick it in a ${m.block} block.</p>`}</div>
    </section>`;
  }
  A.acts["a-open-preset"] = (el) => { A.selectPreset(el.dataset.id); A.goto("edit"); };

  function confirmBar() {
    const names = [...v.checked];
    const users = [...new Set(names.flatMap((n) => A.usage(v.kind, n).map((p) => p.name)))];
    return `<div class="aconfirm" role="alertdialog" aria-label="Confirm delete">${A.icon("trash")}<span><b>Delete ${names.length === 1 ? esc(names[0]) : `${names.length} files`} from the pedal?</b>
      ${users.length ? `${esc(users.join(", "))} ${users.length === 1 ? "uses" : "use"} ${names.length === 1 ? "it" : "them"}. Those presets stay saved but cannot load until you pick another file.` : "No preset uses them."} You cannot undo this.</span>
      <button class="btn" data-act="a-delete-no">Cancel</button><button class="btn btn--danger" data-act="a-delete-yes">Delete ${names.length === 1 ? "file" : `${names.length} files`}</button></div>`;
  }

  function main() {
    const m = KINDS[v.kind];
    return `<div class="astage">
        <div class="atools"><label class="search">${A.icon("search")}<input id="a-q" type="search" value="${esc(v.query)}" placeholder="Find a file in ${m.label}" aria-label="Find a file" data-fk="a-q"></label>
          <div class="seg" role="group" aria-label="Sort">${[["name", "Name"], ["size", "Size"], ["used", "Most used"]].map(([k, l]) => `<button class="btn btn--sm" data-act="a-sort" data-sort="${k}" aria-pressed="${v.sort === k}">${l}</button>`).join("")}</div>
          <span class="ahint">Drop ${m.ext} files anywhere to upload</span></div>
        ${missingNote()}${queueRows()}
        <div class="alist scroll-y" data-keep-scroll="alist-${v.kind}">${rows()}</div>
      </div>${drawer()}
      ${v.drag ? `<div class="adrop"><div>${A.icon("upload")}<b>Drop to upload</b><span>.nam files go to NAM models. .wav files go to ${v.kind === "reverb-irs" ? "Reverb IRs" : "Cabinet IRs"}.</span></div></div>` : ""}`;
  }

  function rail() {
    const m = KINDS[v.kind];
    const n = v.checked.size;
    // Delete asks in the context rail, as the pedal does for destructive actions.
    if (v.confirm && n) return confirmBar();
    return `<button class="btn btn--primary" data-act="a-upload">${A.icon("upload")}<span class="lbl">Upload ${m.ext}</span></button>
      ${v.kind === "models" ? `<button class="btn" data-act="a-tone3000">${A.icon("search")}<span class="lbl">Browse TONE3000</span></button>` : ""}
      ${n ? `<span class="acount">${n} selected</span><button class="btn btn--danger" data-act="a-delete">${A.icon("trash")}<span class="lbl">Delete ${n}</span></button><button class="btn btn--quiet" data-act="a-checkall">Clear</button>` : ""}
      <span class="push"></span><button class="btn" data-act="goto-edit">${A.icon("left")}<span class="lbl">Back to edit</span></button>`;
  }

  // Esc closes the confirm bar, then the drawer.
  const escape = () => {
    if (v.confirm) { v.confirm = false; A.repaint(); return true; }
    if (v.open) { v.open = null; v.renaming = null; A.repaint(); return true; }
    return true;
  };
  A.assetsView = { bar, main, rail, escape, state: v, KINDS, setKind: (k) => { if (KINDS[k]) { v.kind = k; v.open = null; } } };
})();
