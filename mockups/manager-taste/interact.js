/* Ardor Manager mockups: delegated interaction layer.
   - data-act="..." clicks
   - drag to reorder: [data-sort-list] > [data-sort-item] (mouse, touch long-press, Alt+arrows)
   - travel scales: .scale[data-uid][data-key] (drag, arrows, wheel when focused, double-click resets)
   - module drawer, toast, shortcuts, render loop with focus and scroll restore */
(function () {
  "use strict";
  const A = window.Ardor;
  const { DEFS, FAMILIES } = A.D;
  A.acts = {};
  A.onSort = {};

  /* ---------- render loop ---------- */
  let painter = null;
  let lastSel = null;
  const paint = () => {
    if (!painter) return;
    const fk = document.activeElement?.dataset?.fk;
    const scrolls = [...document.querySelectorAll("[data-keep-scroll]")].map((el) => [el.dataset.keepScroll, el.scrollLeft, el.scrollTop]);
    painter(A.get());
    scrolls.forEach(([id, l, t]) => { const el = document.querySelector(`[data-keep-scroll="${id}"]`); if (el) { el.scrollLeft = l; el.scrollTop = t; } });
    if (fk) document.querySelector(`[data-fk="${CSS.escape(fk)}"]`)?.focus({ preventScroll: true });
    const sel = A.get().sel;
    if (sel && sel !== lastSel) {
      requestAnimationFrame(() => document.querySelectorAll(`[data-fk="chip-${CSS.escape(sel)}"], [data-fk="card-${CSS.escape(sel)}"]`)
        .forEach((el) => el.scrollIntoView({ inline: "nearest", block: "nearest" })));
    }
    lastSel = sel;
    const fc = A.get().focusCtl;
    if (fc) document.querySelector(`[data-ctl="${CSS.escape(fc)}"]`)?.classList.add("is-focus");
  };
  A.mount = (fn) => { painter = fn; A.subscribe(paint); paint(); };
  A.repaint = paint;

  /* ---------- toast ---------- */
  let toastEl = null;
  let toastTimer = 0;
  A.toast = (msg) => {
    if (!toastEl) { toastEl = document.createElement("div"); toastEl.className = "toast"; toastEl.setAttribute("role", "status"); document.body.appendChild(toastEl); }
    toastEl.innerHTML = `${A.icon("check")}<span>${A.esc(msg)}</span>`;
    toastEl.classList.add("is-on");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toastEl.classList.remove("is-on"), 2600);
  };

  /* ---------- click actions ---------- */
  const parse = (v) => (v === "true" ? true : v === "false" ? false : v);
  const builtIn = {
    select: (el) => A.view({ sel: el.dataset.uid }),
    toggle: (el) => A.toggle(el.dataset.uid),
    remove: (el) => A.remove(el.dataset.uid || A.get().sel),
    dup: (el) => A.duplicate(el.dataset.uid || A.get().sel),
    insert: (el) => A.openModules(el.dataset.list, Number(el.dataset.at)),
    preset: (el) => A.selectPreset(el.dataset.id),
    live: (el) => A.makeLive(el.dataset.id || undefined, el.dataset.scene || undefined),
    undo: () => A.undo(),
    redo: () => A.redo(),
    save: () => A.save(),
    choice: (el) => A.setParam(el.dataset.uid, el.dataset.key, parse(el.dataset.value)),
    asset: (el) => A.setAsset(el.dataset.uid, el.dataset.value),
    "clear-ov": (el) => A.clearOv(el.dataset.uid, el.dataset.key),
    "eq-band": (el) => A.view({ eqBand: Number(el.dataset.band) }),
    scene: (el) => A.view({ scene: el.dataset.scene || null }),
    "close-sel": () => A.view({ sel: null }),
    "new-preset": (el) => A.newPreset(Number(el.dataset.bank), Number(el.dataset.slot)),
    audition: () => A.view({ audition: !A.get().audition }),
  };
  A.newPreset = (bankN, slot) => {
    if (!bankN || !slot) return;
    const id = `p${bankN}${slot}-${Date.now() % 100000}`;
    const blocks = [A.D.newBlock("nam", true, {}, { asset: "Clean Twin.nam" }), A.D.newBlock("cab", true, {}, { asset: "1x12 Blue.wav" })];
    A.set((s) => ({
      ...s,
      presets: { ...s.presets, [id]: { id, name: "New Preset", blocks, inputDb: 0, outputDb: 0, scenes: null } },
      banks: s.banks.map((b) => (b.n === bankN ? { ...b, slots: b.slots.map((x, i) => (i === slot - 1 ? id : x)) } : b)),
      editId: id, sel: null, scene: null,
    }));
    A.toast(`New preset in bank ${bankN}, slot ${slot}`);
  };

  let swallowClick = false;
  document.addEventListener("click", (ev) => {
    if (swallowClick) { swallowClick = false; ev.stopPropagation(); ev.preventDefault(); return; }
    const el = ev.target.closest("[data-act]");
    if (!el || el.closest("[aria-disabled='true']")) return;
    const act = el.dataset.act;
    const fn = A.acts[act] || builtIn[act];
    if (!fn) return;
    ev.stopPropagation();
    fn(el, ev);
  }, true);

  /* ---------- drag to reorder ---------- */
  let drag = null;
  const itemsOf = (list) => [...list.children].filter((c) => c.hasAttribute("data-sort-item"));
  const LONG_PRESS = 260;

  document.addEventListener("pointerdown", (ev) => {
    if (ev.button !== 0) return;
    const item = ev.target.closest("[data-sort-item]");
    if (!item || ev.target.closest(".scale, .blk__pow, input, select, .ins")) return;
    if (item.dataset.uid === "") return;
    const list = item.parentElement?.closest("[data-sort-list]");
    if (!list || item.parentElement !== list) return;
    const handleSel = item.dataset.sortHandle;
    const touch = ev.pointerType !== "mouse";
    if (!touch && handleSel && !ev.target.closest(handleSel)) return;
    drag = { item, list, x0: ev.clientX, y0: ev.clientY, x: ev.clientX, y: ev.clientY, started: false, touch, id: ev.pointerId, timer: 0 };
    if (touch) drag.timer = setTimeout(() => { if (drag && !drag.started) start(); }, LONG_PRESS);
  });

  function start() {
    const { item } = drag;
    const r = item.getBoundingClientRect();
    drag.started = true;
    drag.dx = drag.x - r.left;
    drag.dy = drag.y - r.top;
    const ghost = item.cloneNode(true);
    ghost.classList.add("drag-ghost");
    ghost.classList.remove("is-sel");
    ghost.style.width = `${r.width}px`;
    ghost.style.height = `${r.height}px`;
    document.body.appendChild(ghost);
    drag.ghost = ghost;
    drag.mark = document.createElement("div");
    drag.mark.className = drag.list.dataset.axis === "y" ? "drop-mark drop-mark--y" : "drop-mark";
    item.classList.add("is-dragging-src");
    document.body.classList.add("is-sorting");
    if (navigator.vibrate && drag.touch) navigator.vibrate(8);
    move();
  }

  function targetList() {
    drag.ghost.style.display = "none";
    const under = document.elementFromPoint(drag.x, drag.y);
    drag.ghost.style.display = "";
    const kind = drag.list.dataset.sortKind || "chain";
    let list = under?.closest("[data-sort-list]");
    while (list && ((list.dataset.sortKind || "chain") !== kind || drag.item.contains(list))) list = list.parentElement?.closest("[data-sort-list]");
    if (list && drag.item.classList.contains("rig") && list.dataset.sortList !== "main") list = document.querySelector('[data-sort-list="main"]');
    return list || drag.lastList || drag.list;
  }

  function move() {
    drag.ghost.style.transform = `translate(${drag.x - drag.dx}px, ${drag.y - drag.dy}px) rotate(-1.5deg)`;
    const list = targetList();
    drag.lastList = list;
    if (list.dataset.sortMode === "swap") { swapTarget(list); autoScroll(); return; }
    const axis = list.dataset.axis || "x";
    const items = itemsOf(list).filter((i) => i !== drag.item);
    let index = items.length;
    for (let i = 0; i < items.length; i++) {
      const r = items[i].getBoundingClientRect();
      const before = axis === "y" ? drag.y < r.top + r.height / 2
        : axis === "grid" ? (drag.y < r.top) || (drag.y <= r.bottom && drag.x < r.left + r.width / 2)
          : drag.x < r.left + r.width / 2;
      if (before) { index = i; break; }
    }
    drag.index = index;
    const ref = items[index];
    if (ref) list.insertBefore(drag.mark, ref.previousElementSibling?.classList.contains("ins") ? ref.previousElementSibling : ref);
    else {
      const last = items[items.length - 1];
      if (last) last.after(drag.mark); else list.prepend(drag.mark);
    }
    autoScroll();
  }

  // Swap mode: the slot under the pointer is the target; the two items change places.
  function swapTarget(list) {
    drag.ghost.style.display = "none";
    const under = document.elementFromPoint(drag.x, drag.y)?.closest("[data-sort-item]");
    drag.ghost.style.display = "";
    document.querySelectorAll(".is-swap").forEach((el) => el.classList.remove("is-swap"));
    const items = itemsOf(list);
    const target = under && items.includes(under) ? under : null;
    if (target && target !== drag.item) target.classList.add("is-swap");
    drag.index = target ? items.indexOf(target) : -1;
  }

  function autoScroll() {
    const sc = drag.item.closest(".scroll-x, .scroll-y") || drag.lastList.closest(".scroll-x, .scroll-y");
    if (!sc) return;
    const r = sc.getBoundingClientRect();
    const edge = 70;
    let vx = 0;
    let vy = 0;
    if (drag.x < r.left + edge) vx = -12; else if (drag.x > r.right - edge) vx = 12;
    if (drag.y < r.top + edge) vy = -10; else if (drag.y > r.bottom - edge) vy = 10;
    if (vx || vy) { sc.scrollLeft += vx; sc.scrollTop += vy; cancelAnimationFrame(drag.raf); drag.raf = requestAnimationFrame(() => drag && drag.started && move()); }
  }

  document.addEventListener("pointermove", (ev) => {
    if (!drag || ev.pointerId !== drag.id) return;
    drag.x = ev.clientX; drag.y = ev.clientY;
    if (!drag.started) {
      const dist = Math.hypot(drag.x - drag.x0, drag.y - drag.y0);
      if (drag.touch) { if (dist > 10) { clearTimeout(drag.timer); drag = null; } return; }
      if (dist > 5) start(); else return;
    }
    ev.preventDefault();
    move();
  }, { passive: false });

  document.addEventListener("touchmove", (ev) => { if (drag && drag.started) ev.preventDefault(); }, { passive: false });

  const endDrag = (commit) => {
    if (!drag) return;
    clearTimeout(drag.timer);
    const d = drag;
    drag = null;
    if (!d.started) return;
    cancelAnimationFrame(d.raf);
    d.ghost.remove();
    d.mark.remove();
    document.querySelectorAll(".is-swap").forEach((el) => el.classList.remove("is-swap"));
    d.item.classList.remove("is-dragging-src");
    document.body.classList.remove("is-sorting");
    swallowClick = true;
    setTimeout(() => { swallowClick = false; }, 0);
    if (!commit) return;
    const kind = d.list.dataset.sortKind || "chain";
    const handler = A.onSort[kind];
    if (d.index < 0) return;
    if (handler) handler({ uid: d.item.dataset.uid, fromList: d.list.dataset.sortList, toList: d.lastList.dataset.sortList, index: d.index });
  };
  document.addEventListener("pointerup", (ev) => { if (drag && ev.pointerId === drag.id) endDrag(true); });
  document.addEventListener("pointercancel", () => endDrag(false));

  // Default chain reorder: index is the position without the dragged block.
  A.onSort.chain = ({ uid, toList, index }) => {
    const p = A.cur();
    const hit = A.locate(p, uid);
    if (!hit) return;
    if (hit.listId === toList && hit.index === index) return;
    A.edit((pp) => A.insertAt(A.removeFrom(pp, uid), toList, index, hit.block));
    if (A.get().sel) A.view({ sel: uid });
    A.announce(`${A.blockName(hit.block)} moved to position ${index + 1}`);
  };

  /* ---------- live region ---------- */
  let live = null;
  A.announce = (msg) => {
    if (!live) { live = document.createElement("div"); live.className = "sr"; live.setAttribute("aria-live", "polite"); document.body.appendChild(live); }
    live.textContent = msg;
  };

  /* ---------- travel scales ---------- */
  let slide = null;
  const snap = (p, v) => {
    const step = p.step || 0.01;
    const s = Math.round((v - p.min) / step) * step + p.min;
    return Math.min(p.max, Math.max(p.min, Number(s.toFixed(4))));
  };
  const paramOf = (el) => A.param(el.dataset.def, el.dataset.key);
  const valueAt = (p, rect, x) => snap(p, p.min + ((x - rect.left) / rect.width) * (p.max - p.min));

  document.addEventListener("pointerdown", (ev) => {
    const el = ev.target.closest(".scale");
    if (!el || ev.button !== 0) return;
    ev.preventDefault();
    const p = paramOf(el);
    if (!p) return;
    const rect = el.getBoundingClientRect();
    el.focus({ preventScroll: true });
    slide = { uid: el.dataset.uid, key: el.dataset.key, p, rect, id: ev.pointerId, first: true };
    const v = valueAt(p, rect, ev.clientX);
    A.setParam(slide.uid, slide.key, v, { record: true });
    slide.first = false;
  });
  document.addEventListener("pointermove", (ev) => {
    if (!slide || ev.pointerId !== slide.id) return;
    const v = valueAt(slide.p, slide.rect, ev.clientX);
    A.setParam(slide.uid, slide.key, v, { record: false });
  });
  document.addEventListener("pointerup", () => { slide = null; });

  let lastKey = { k: "", t: 0 };
  const nudge = (el, dir, big, fine) => {
    const p = paramOf(el);
    const preset = A.cur();
    const hit = A.locate(preset, el.dataset.uid);
    if (!p || !hit) return;
    const cur = A.val(preset, hit.block, p.key, A.get().scene);
    const step = (p.step || 0.01) * (big ? 10 : 1) * (fine ? 0.2 : 1);
    const k = `${el.dataset.uid}:${p.key}`;
    const now = Date.now();
    const record = !(lastKey.k === k && now - lastKey.t < 900);
    lastKey = { k, t: now };
    A.setParam(el.dataset.uid, p.key, Math.min(p.max, Math.max(p.min, Number((cur + dir * step).toFixed(4)))), { record });
  };
  document.addEventListener("wheel", (ev) => {
    const el = ev.target.closest(".scale");
    const hoverMode = document.body.dataset.wheel === "hover";
    if (!el || (!hoverMode && document.activeElement !== el)) return;
    ev.preventDefault();
    nudge(el, ev.deltaY < 0 || ev.deltaX > 0 ? 1 : -1, false, ev.shiftKey);
  }, { passive: false });
  document.addEventListener("dblclick", (ev) => {
    const el = ev.target.closest(".scale");
    if (!el) return;
    const p = paramOf(el);
    A.setParam(el.dataset.uid, el.dataset.key, p.def);
    A.toast(`${p.label} reset to ${A.fmtText(p, p.def)}`);
  });
  document.addEventListener("focusin", (ev) => {
    const el = ev.target.closest?.(".scale");
    document.querySelectorAll(".ctl.is-focus").forEach((c) => c.classList.remove("is-focus"));
    if (el) {
      const k = `${el.dataset.uid}:${el.dataset.key}`;
      el.closest(".ctl")?.classList.add("is-focus");
      if (A.get().focusCtl !== k) A.set((s) => ({ ...s, focusCtl: k }), { record: false, dirty: false });
    }
  });

  /* ---------- EQ band drag ---------- */
  let band = null;
  document.addEventListener("pointerdown", (ev) => {
    const c = ev.target.closest("circle.band");
    if (!c) return;
    ev.preventDefault();
    const svg = c.ownerSVGElement;
    const vb = svg.viewBox.baseVal;
    band = { i: Number(c.dataset.band), rect: svg.getBoundingClientRect(), w: vb.width, h: vb.height, first: true, uid: A.get().sel };
    A.view({ eqBand: band.i });
  });
  document.addEventListener("pointermove", (ev) => {
    if (!band) return;
    const x = ((ev.clientX - band.rect.left) / band.rect.width) * band.w;
    const y = ((ev.clientY - band.rect.top) / band.rect.height) * band.h;
    const f = Math.round(Math.min(20000, Math.max(20, 20 * Math.pow(1000, x / band.w))));
    const g = Math.round(Math.min(18, Math.max(-18, ((band.h / 2 - y) / (band.h / 2)) * 18)) * 10) / 10;
    A.edit((p) => A.mapBlock(p, band.uid, (b) => ({ ...b, bands: b.bands.map((bd, i) => (i === band.i ? { ...bd, f, g } : bd)) })), { record: band.first });
    band.first = false;
  });
  document.addEventListener("pointerup", () => { band = null; });

  /* ---------- module drawer ---------- */
  let mods = null;
  let modTarget = null;
  let famFilter = "all";
  const groupOn = (listId, group, exceptUid) => {
    const list = A.getList(A.cur(), listId);
    return list.find((b) => b.uid !== exceptUid && b.on && DEFS[b.def].group === group);
  };
  function renderMods(q = "") {
    const needle = q.trim().toLowerCase();
    const defs = Object.values(DEFS).filter((d) => d.id !== "dualAmp" && d.id !== "global" && (famFilter === "all" || d.fam === famFilter)
      && (!needle || d.name.toLowerCase().includes(needle) || d.code.toLowerCase().includes(needle) || d.cap.toLowerCase().includes(needle)));
    const inLane = modTarget.listId !== "main";
    const html = FAMILIES.map((f) => {
      const rows = defs.filter((d) => d.fam === f.id && !(inLane && d.id === "dualRig"));
      if (!rows.length) return "";
      return `<div class="mods__grp"><span>${f.label}</span><span>${rows.length}</span></div>${rows.map((d) => {
        const clash = d.group && groupOn(modTarget.listId, d.group);
        return `<button class="mod-row f-${d.fam}" data-act="add-block" data-def="${d.id}"><span class="code">${d.code}</span>
          <span><b>${A.esc(d.name)}</b><small>${clash ? `Turns off ${A.esc(A.blockName(clash))}. One ${A.esc(d.cap.toLowerCase())} block runs at a time.` : A.esc(d.desc)}</small></span>
          <span class="plus">${A.icon("plus")}</span></button>`;
      }).join("")}`;
    }).join("");
    mods.querySelector(".mods__list").innerHTML = html || '<p style="padding:24px 8px;color:var(--bone-2)">No block matches. Try a family name, for example “delay”.</p>';
    mods.querySelectorAll(".fams button").forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.fam === famFilter)));
  }
  function buildMods() {
    const scrim = document.createElement("div");
    scrim.className = "mods-scrim";
    scrim.hidden = true;
    scrim.addEventListener("click", () => A.closeModules());
    mods = document.createElement("aside");
    mods.className = "mods";
    mods.setAttribute("role", "dialog");
    mods.setAttribute("aria-label", "Add a block");
    mods.innerHTML = `<div class="mods__head">
        <div class="mods__title"><div><h2>Add block</h2><p data-where></p></div><button class="btn btn--icon" data-act="close-mods" aria-label="Close">${A.icon("close")}</button></div>
        <label class="search">${A.icon("search")}<input type="search" placeholder="Search 51 blocks" aria-label="Search blocks"></label>
        <div class="fams"><button class="f-cab" data-fam="all" style="--fam:var(--bone)">All</button>${FAMILIES.map((f) => `<button class="f-${f.id}" data-fam="${f.id}">${f.label}</button>`).join("")}</div>
      </div><div class="mods__list"></div>`;
    document.body.append(scrim, mods);
    mods.scrim = scrim;
    const input = mods.querySelector("input");
    input.addEventListener("input", () => renderMods(input.value));
    input.addEventListener("keydown", (e) => { if (e.key === "Enter") mods.querySelector(".mod-row")?.click(); });
    mods.querySelector(".fams").addEventListener("click", (e) => { const b = e.target.closest("button"); if (b) { famFilter = b.dataset.fam; renderMods(input.value); } });
  }
  A.openModules = (listId = "main", index) => {
    if (!mods) buildMods();
    const list = A.getList(A.cur(), listId);
    const at = Number.isFinite(index) ? index : list.length;
    modTarget = { listId, index: at };
    const after = list[at - 1];
    const lane = listId === "main" ? "" : ` in lane ${listId.split(":")[1].toUpperCase()}`;
    mods.querySelector("[data-where]").textContent = after ? `Insert after ${A.blockName(after)}${lane}, position ${at + 1}` : `Insert at the start${lane}`;
    mods.scrim.hidden = false;
    requestAnimationFrame(() => { mods.scrim.classList.add("is-open"); mods.classList.add("is-open"); });
    const input = mods.querySelector("input");
    input.value = "";
    renderMods("");
    setTimeout(() => input.focus(), 60);
  };
  A.closeModules = () => {
    if (!mods || !mods.classList.contains("is-open")) return false;
    mods.classList.remove("is-open");
    mods.scrim.classList.remove("is-open");
    setTimeout(() => { mods.scrim.hidden = true; }, 220);
    return true;
  };
  A.acts["close-mods"] = () => A.closeModules();
  A.acts["add-block"] = (el) => { const off = A.add(el.dataset.def, modTarget.listId, modTarget.index); A.closeModules(); if (!off.length) A.toast(`${DEFS[el.dataset.def].name} added`); };

  /* ---------- keyboard ---------- */
  const typing = (t) => t.closest?.("input, textarea, select, [contenteditable]");
  A.keys = {};
  document.addEventListener("keydown", (ev) => {
    const mod = ev.metaKey || ev.ctrlKey;
    const s = A.get();
    if (A.keys[ev.key] && !typing(ev.target) && A.keys[ev.key](ev) === true) return;
    if (ev.key === "Escape") { if (A.closeModules()) return; if (A.keys.Escape?.(ev)) return; if (s.sel) A.view({ sel: null }); return; }
    if (mod && ev.key.toLowerCase() === "z") { ev.preventDefault(); if (ev.shiftKey) A.redo(); else A.undo(); return; }
    if (mod && ev.key.toLowerCase() === "s") { ev.preventDefault(); A.save(); return; }
    if (mod && ev.key.toLowerCase() === "d" && s.sel) { ev.preventDefault(); A.duplicate(s.sel); return; }
    if (typing(ev.target) || ev.target.closest?.(".scale")) {
      const el = ev.target.closest?.(".scale");
      if (!el) return;
      const map = { ArrowRight: 1, ArrowUp: 1, ArrowLeft: -1, ArrowDown: -1, PageUp: 10, PageDown: -10 };
      if (ev.key in map) { ev.preventDefault(); nudge(el, Math.sign(map[ev.key]), Math.abs(map[ev.key]) === 10, ev.shiftKey); }
      return;
    }
    const card = ev.target.closest?.("[data-sort-item][data-uid]");
    const uid = card?.dataset.uid || s.sel;
    if (!uid) return;
    const hit = A.locate(A.cur(), uid);
    if (!hit) return;
    if (ev.altKey && (ev.key === "ArrowLeft" || ev.key === "ArrowRight")) {
      ev.preventDefault();
      const list = A.getList(A.cur(), hit.listId);
      const to = Math.min(list.length - 1, Math.max(0, hit.index + (ev.key === "ArrowRight" ? 1 : -1)));
      if (to !== hit.index) { A.onSort.chain({ uid, toList: hit.listId, index: to }); requestAnimationFrame(() => document.querySelector(`[data-fk="card-${uid}"]`)?.focus()); }
      return;
    }
    if (ev.key === "ArrowLeft" || ev.key === "ArrowRight") {
      const all = A.allBlocks(A.cur());
      const i = all.findIndex((b) => b.uid === uid);
      const next = all[i + (ev.key === "ArrowRight" ? 1 : -1)];
      if (next) { ev.preventDefault(); A.view({ sel: next.uid }); requestAnimationFrame(() => document.querySelector(`[data-fk="card-${next.uid}"]`)?.focus()); }
      return;
    }
    if (ev.key === "Enter" && card) { A.view({ sel: uid }); return; }
    if (ev.key.toLowerCase() === "b") { A.toggle(uid); return; }
    if (ev.key === "Delete" || ev.key === "Backspace") { ev.preventDefault(); A.remove(uid); return; }
    if (ev.key === "a" || ev.key === "+") { ev.preventDefault(); A.openModules(hit.listId, hit.index + 1); }
  });
})();
