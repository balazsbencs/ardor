/* Ardor Manager mockups: app-bar pieces shared by the five directions. */
(function () {
  "use strict";
  const A = window.Ardor;
  const esc = A.esc;

  A.mark = () => '<a class="mark" href="index.html" title="All mockups"><i></i>Ardor</a>';

  // PRODUCT.md: LAN access is plain HTTP and the UI must keep saying so.
  A.conn = () => `<div class="conn" title="Connected over the local network. LAN access uses plain HTTP: use it on trusted networks only.">
      <span class="dot" aria-hidden="true"></span><b>Ardor Pedal</b><span class="addr">192.168.88.12</span><span class="http">HTTP</span></div>`;

  A.liveState = () => {
    const s = A.get();
    const isLive = s.live.presetId === s.editId;
    if (isLive && s.audition) return '<span class="tag tag--live" title="You hear every change on the pedal. Save writes it to the slot.">LIVE ON PEDAL</span>';
    if (isLive) return '<span class="tag tag--line">LIVE · AUDITION OFF</span>';
    return `<button class="btn btn--sm" data-act="live" title="Make this preset live on the pedal">${A.icon("send")}Load on pedal</button>`;
  };

  A.dirtyTag = () => (A.isDirty() ? '<span class="tag tag--warn">MODIFIED</span>' : "");

  A.editTools = (o = {}) => `
    <button class="btn btn--icon btn--quiet" data-act="undo" aria-label="Undo" title="Undo (⌘Z)" ${A.canUndo() ? "" : "disabled"}>${A.icon("undo")}</button>
    <button class="btn btn--icon btn--quiet" data-act="redo" aria-label="Redo" title="Redo (⇧⌘Z)" ${A.canRedo() ? "" : "disabled"}>${A.icon("redo")}</button>
    <button class="btn ${A.isDirty() ? "btn--primary" : ""}" data-act="save" title="Save to slot (⌘S)" ${A.isDirty() ? "" : "disabled"}>${A.icon("save")}<span class="lbl">${o.saveLabel || "Save"}</span></button>`;

  A.presetTitle = () => {
    const p = A.cur();
    const slot = A.slotOf(p.id);
    return `<div class="where"><b>${esc(p.name)}</b><span>Bank ${String(slot.bank).padStart(2, "0")} · FS ${slot.slot}</span></div>`;
  };

  A.mockflag = (n) => `<a class="mockflag" href="index.html" title="Demo data. Back to all mockups.">Mockup ${n}/5 · demo data</a>`;

  A.globalPanel = (p, o = {}) => {
    const s = A.get();
    const g = A.globalBlock(p);
    const exp = p.exp ? A.locate(p, p.exp.uid) : null;
    const expParam = exp ? A.param(exp.block.def, p.exp.key) : null;
    return `<div class="gpanel ${o.cls || ""}">
      ${A.control(p, g, A.param("global", "inputDb"), { scene: s.scene, compact: o.compact })}
      ${A.control(p, g, A.param("global", "outputDb"), { scene: s.scene, compact: o.compact })}
      <div class="ctl f-cab gpanel__lock"><div class="ctl__top"><span class="ctl__lbl">Safety limiter</span><span class="tag tag--line">FIXED</span></div>
        <div class="ctl__val">-1<small>dBFS</small></div><p class="note">Protection, not a tone control. It cannot be changed.</p></div>
      <div class="ctl f-dly"><div class="ctl__top"><span class="ctl__lbl">Expression pedal</span>${A.icon("pedal")}</div>
        ${exp ? `<div class="ctl__val" style="font-size:22px">${esc(A.blockName(exp.block))} · ${esc(expParam.label)}</div><p class="note">Heel ${A.fmtText(expParam, p.exp.min)} to toe ${A.fmtText(expParam, p.exp.max)}</p>` : '<p class="note">Not assigned. Pick a control, then Assign EXP.</p>'}</div>
    </div>`;
  };

  A.sceneSeg = (p, o = {}) => {
    const s = A.get();
    if (!p.scenes) return o.empty ?? "";
    const liveScene = s.live.presetId === p.id ? s.live.sceneId : null;
    return `<div class="seg" role="group" aria-label="Edit scope">
      <button class="btn btn--sm" data-act="scene" data-scene="" aria-pressed="${!s.scene}">Preset</button>
      ${p.scenes.map((sc, i) => `<button class="btn btn--sm" data-act="scene" data-scene="${sc.id}" aria-pressed="${s.scene === sc.id}" title="Edit scene ${i + 1}">${i + 1} ${esc(sc.name)}${liveScene === sc.id ? '<i class="lampdot" title="Live scene"></i>' : ""}</button>`).join("")}
    </div>`;
  };
})();
