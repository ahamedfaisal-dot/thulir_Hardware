/* CryoAxis / THULIR dashboard front-end (vanilla JS, hash-routed single page) */
"use strict";

// ── helpers ──────────────────────────────────────────────────────
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
const isNum = (v) => v !== null && v !== undefined && v !== "" && !Number.isNaN(Number(v));
const num = (v, d = 1) => (isNum(v) ? Number(v).toFixed(d) : "—");
const signed = (v, d = 1) => (isNum(v) ? (Number(v) > 0 ? "+" : "") + Number(v).toFixed(d) : "—");
function hms(sec) {
  if (!isNum(sec)) return "—";
  sec = Math.max(0, Math.round(sec));
  const p = (n) => String(n).padStart(2, "0");
  return `${p(Math.floor(sec / 3600))}:${p(Math.floor((sec % 3600) / 60))}:${p(sec % 60)}`;
}
function dur(sec) {
  if (!isNum(sec)) return "—";
  const m = Math.round(sec / 60);
  return m >= 60 ? `${Math.floor(m / 60)} h ${m % 60} min` : `${m} min`;
}
function shortTime(iso) { return iso ? iso.slice(11, 16) : ""; }
function shortDate(iso) {
  if (!iso) return "—";
  const d = new Date(iso);
  return isNaN(d) ? iso : d.toLocaleDateString(undefined, { day: "2-digit", month: "short", year: "numeric" });
}
function fullTime(iso) { return iso ? iso.replace("T", " ") : "—"; }

const IC = {
  home: '<path d="M3 11l9-8 9 8M5 10v10h5v-6h4v6h5V10"/>',
  activity: '<path d="M3 12h4l3-8 4 16 3-8h4"/>',
  list: '<path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01"/>',
  clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
  book: '<path d="M4 4h12a4 4 0 014 4v12H8a4 4 0 01-4-4zM4 16a4 4 0 014-4h12"/>',
  gear: '<circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M2 12h3M19 12h3M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1"/>',
  therm: '<path d="M14 14.8V5a2 2 0 00-4 0v9.8a4 4 0 104 0z"/>',
  drop: '<path d="M12 3s6 6.5 6 11a6 6 0 01-12 0c0-4.5 6-11 6-11z"/>',
  play: '<path d="M7 4l13 8-13 8z" fill="currentColor"/>',
  pause: '<path d="M7 5h3v14H7zM14 5h3v14h-3z" fill="currentColor"/>',
  stop: '<circle cx="12" cy="12" r="9"/><path d="M6 6l12 12"/>',
  file: '<path d="M6 3h8l4 4v14H6zM14 3v4h4M9 12h6M9 16h6"/>',
  bell: '<path d="M6 16V11a6 6 0 1112 0v5l2 2H4zM10 21h4"/>',
  eye: '<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
  dl: '<path d="M12 4v11M7 11l5 5 5-5M5 20h14"/>',
  pip: '<path d="M14 3l7 7-2 2-2-2-7 7-3 1-3 3-1-1 3-3 1-3 7-7-2-2z"/>',
  profile: '<path d="M3 3v18h18M7 15l4-5 3 3 5-7"/>',
  run: '<path d="M4 20V10M10 20V4M16 20v-8M22 20H2"/>',
};
const svg = (n, cls = "") => `<svg class="${cls}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">${IC[n] || ""}</svg>`;

async function api(path, opts = {}) {
  const o = { headers: { "Content-Type": "application/json" }, ...opts };
  if (o.body && typeof o.body !== "string") o.body = JSON.stringify(o.body);
  let res, body = null;
  try {
    res = await fetch(path, o);
    try { body = await res.json(); } catch (_) { body = null; }
  } catch (e) {
    return { ok: false, status: 0, body: { message: "Server not reachable." } };
  }
  return { ok: res.ok, status: res.status, body };
}

function toast(msg, kind = "") {
  const el = document.createElement("div");
  el.className = "toast " + kind;
  el.textContent = msg;
  $("toasts").appendChild(el);
  setTimeout(() => el.remove(), kind === "err" ? 7000 : 3500);
}

function confirmDialog(title, message, okLabel, danger = true) {
  return new Promise((resolve) => {
    const root = $("modalRoot");
    root.innerHTML = `<div class="modal-back"><div class="modal" role="dialog" aria-modal="true">
      <h3>${esc(title)}</h3><p>${esc(message)}</p>
      <div class="row"><button class="btn" id="mNo">Cancel</button>
      <button class="btn ${danger ? "danger" : "primary"}" id="mYes">${esc(okLabel)}</button></div></div></div>`;
    const done = (v) => { root.innerHTML = ""; resolve(v); };
    $("mNo").onclick = () => done(false);
    $("mYes").onclick = () => done(true);
    $("mNo").focus();
  });
}

// ── state ────────────────────────────────────────────────────────
const S = {
  snap: null, recvAt: 0, page: "home",
  settings: { audio: { enabled: true, language: "en", volume: 1, rate: 1 }, thresholds: { stale_after_s: 10 } },
  guideAvailable: false,
  graph: { run_id: null, samples: [], profile: [], tolerance: null },
  protocols: [], runs: [], alertHistory: [],
  editor: null, historyOpen: null,
  socketOk: false,
};
let charts = {};

// ── navigation ───────────────────────────────────────────────────
const PAGES = [
  ["home", "Home", "home"], ["monitor", "Run Monitor", "activity"], ["protocols", "Protocols", "list"],
  ["history", "Run History", "clock"], ["guide", "User Guide", "book"], ["settings", "Settings", "gear"],
];
function renderNav() {
  $("nav").innerHTML = PAGES.map(([id, label, ic]) =>
    `<button class="nav-item ${S.page === id ? "active" : ""}" data-page="${id}">${svg(ic)}<span>${label}</span></button>`).join("");
  $("nav").querySelectorAll("button").forEach((b) => (b.onclick = () => { location.hash = "#/" + b.dataset.page; }));
}
function route() {
  const id = (location.hash.replace("#/", "") || "home").split("/")[0];
  S.page = PAGES.some((p) => p[0] === id) ? id : "home";
  Object.values(charts).forEach((c) => c && c.destroy());
  charts = {};
  renderNav();
  ({ home: pageHome, monitor: pageMonitor, protocols: pageProtocols, history: pageHistory,
     guide: pageGuide, settings: pageSettings })[S.page]();
}
window.addEventListener("hashchange", route);

// ── clock ────────────────────────────────────────────────────────
function tickClock() {
  const n = new Date();
  $("clockDate").textContent = n.toLocaleDateString(undefined, { weekday: "short", day: "2-digit", month: "short", year: "numeric" });
  $("clockTime").textContent = n.toLocaleTimeString(undefined, { hour: "2-digit", minute: "2-digit" });
}

// ── live elapsed (interpolated between snapshots) ───────────────
function liveRun() {
  const r = S.snap && S.snap.run;
  if (!r) return null;
  let el = r.elapsed_s;
  if (r.active && !r.paused && S.snap.connected) el += (Date.now() - S.recvAt) / 1000;
  const total = r.total_s;
  const out = { ...r, elapsed_s: el };
  if (isNum(total) && total > 0) {
    out.remaining_s = Math.max(total - el, 0);
    out.progress_pct = r.final_status === "COMPLETED" ? 100 : Math.min(Math.max((el / total) * 100, 0), 100);
  }
  return out;
}

// ── chart ────────────────────────────────────────────────────────
function makeProfileChart(canvasId, data) {
  const cv = $(canvasId);
  if (!cv) return null;
  if (typeof Chart === "undefined") {
    cv.parentElement.innerHTML = '<div class="empty">Chart library could not be loaded (internet access to the CDN is required).</div>';
    return null;
  }
  const tol = data.tolerance;
  const prof = data.profile || [];
  const up = tol != null ? prof.map(([x, y]) => ({ x, y: y + tol })) : [];
  const lo = tol != null ? prof.map(([x, y]) => ({ x, y: y - tol })) : [];
  const css = getComputedStyle(document.documentElement);
  const col = (n) => css.getPropertyValue(n).trim();
  const ch = new Chart(cv.getContext("2d"), {
    type: "line",
    data: {
      datasets: [
        { label: "Tolerance Range", data: up, borderWidth: 0, pointRadius: 0, fill: "+1", backgroundColor: col("--c-band"), order: 4 },
        { label: "_lower", data: lo, borderWidth: 0, pointRadius: 0, fill: false, order: 4 },
        { label: "Target Temperature", data: prof.map(([x, y]) => ({ x, y })), borderColor: col("--c-target"), borderWidth: 2, pointRadius: 0, order: 2 },
        { label: "Actual Temperature", data: [], borderColor: col("--c-actual"), backgroundColor: col("--c-actual"), borderWidth: 2, pointRadius: 0, tension: 0.15, spanGaps: false, order: 1 },
      ],
    },
    options: {
      parsing: false, animation: false, responsive: true, maintainAspectRatio: false,
      interaction: { mode: "nearest", intersect: false, axis: "x" },
      scales: {
        x: { type: "linear", min: 0, title: { display: true, text: "Time (minutes)" }, grid: { color: col("--c-grid") } },
        y: { title: { display: true, text: "Temperature (°C)" }, grid: { color: col("--c-grid") } },
      },
      plugins: {
        legend: { position: "top", align: "end", labels: { boxWidth: 10, usePointStyle: true, filter: (i) => i.text !== "_lower" } },
        tooltip: { filter: (i) => i.dataset.label !== "_lower" && i.dataset.label !== "Tolerance Range" },
      },
    },
  });
  ch._tol = tol;
  setActual(ch, data.samples || []);
  return ch;
}
function setActual(ch, samples) {
  if (!ch) return;
  ch.data.datasets[3].data = samples.map((s) => ({ x: (s.t || 0) / 60, y: s.actual }));
  const last = samples.length ? (samples[samples.length - 1].t || 0) / 60 : 0;
  const profEnd = ch.data.datasets[2].data.length ? ch.data.datasets[2].data.at(-1).x : 0;
  ch.options.scales.x.max = Math.max(profEnd, last, 1);
  ch.update("none");
}

async function loadGraph(runId) {
  const q = runId ? "?run=" + encodeURIComponent(runId) : "";
  const r = await api("/api/graph" + q);
  if (r.ok) S.graph = r.body;
  return S.graph;
}
function pushLiveSample(snap) {
  const r = snap.run;
  if (!r || !r.active || r.id !== S.graph.run_id) return;
  if (!snap.connected || snap.last_rx === S.lastRxPushed) return;   // only real new readings
  S.lastRxPushed = snap.last_rx;
  S.graph.samples.push({ t: r.elapsed_s, actual: snap.temperature.actual, target: snap.temperature.target });
  if (S.graph.samples.length > 20000) S.graph.samples.shift();
}

// ── Home ─────────────────────────────────────────────────────────
function pageHome() {
  $("view").innerHTML = `
  <div class="card about" style="margin-bottom:var(--gap)"><b>About CryoAxis.</b> CryoAxis is a low-cost benchtop device developed by the REC-CHENNAI iGEM 2026 team for the cryopreservation of recalcitrant plant tissues. It integrates automated temperature control, pipetting, environmental monitoring, and guided protocols to improve consistency and reduce operator-dependent variation.</div>
  <h1 class="page-title">System Status</h1>
  <div id="connBanner"></div>
  <div class="card grid" style="margin-bottom:var(--gap)">
    <div class="statusbar" style="grid-column:1/-1">
      <div class="status-main"><span id="stBadge" class="status-badge st-OFFLINE">—</span><div class="kv"><div class="k">Device state</div><div class="v" id="stDevice">—</div></div></div>
      <div class="kv"><div class="k">Current Step</div><div class="v" id="stStep">–</div></div>
      <div class="kv"><div class="k">Protocol</div><div class="v" id="stProto">Not Selected</div></div>
      <div class="kv"><div class="k">Hardware</div><div class="v" id="stHw">—</div></div>
      <div class="kv"><div class="k">Data source</div><div class="v">Device telemetry</div></div>
    </div>
  </div>

  <div class="grid g4">
    <div class="card"><div class="card-title">${svg("therm")}Temperature</div>
      <div class="temp-row">
        <div><div class="big mono" id="tActual">—</div><div class="lbl">Actual</div></div>
        <div><div class="big mono" id="tTarget">—</div><div class="lbl">Target</div></div>
      </div>
      <div class="mini-rows">
        <div><span class="muted">Deviation</span><b class="mono" id="tDev">—</b></div>
        <div><span class="muted">Cooling Rate</span><b class="mono rate" id="tRate">—</b></div>
        <div><span class="muted">Tolerance</span><span id="tTol">—</span></div>
      </div></div>
    <div class="card"><div class="card-title">${svg("drop")}Humidity</div>
      <div class="center"><div class="big hum mono" id="hVal">—</div><div class="lbl">Relative Humidity</div>
      <div class="mini-rows"><div><span class="muted">Sensor</span><span id="hState">—</span></div>
      <div><span class="muted">Last reading</span><span id="hTs">—</span></div></div></div></div>
    <div class="card"><div class="card-title">${svg("clock")}Process Timing</div>
      <div class="timing"><div><div class="big mono" id="pEl">—</div><div class="lbl">Elapsed Time</div></div>
        <div><div class="big mono rem" id="pRem">—</div><div class="lbl">Remaining Time</div></div></div>
      <div class="prog-row"><div class="progress"><i id="pBar"></i></div><b class="mono" id="pPct">—</b></div>
      <div class="mini-rows"><div><span class="muted">Total duration</span><span class="mono" id="pTotal">—</span></div></div></div>
    <div class="card placeholder-card"><div class="card-title">${svg("pip")}Automated Pipetting</div>
      <div class="status"><span class="dot"></span>Status: Not Implemented</div>
      <p>Automated pipetting functionality will be available in a future hardware update.</p>
      ${svg("pip", "pip")}</div>
  </div>

  <div class="grid g-profile">
    <div class="card"><div class="card-title">${svg("profile")}Temperature Profile <span class="link muted" id="gRun" style="text-decoration:none"></span></div>
      <div class="chart-wrap"><canvas id="homeChart"></canvas></div></div>
    <div class="card"><div class="card-title">${svg("gear")}Process Controls</div>
      <div class="controls">
        <button class="btn primary" data-cmd="start">${svg("play")}START</button>
        <button class="btn" data-cmd="pause">${svg("pause")}PAUSE</button>
        <button class="btn" data-cmd="resume">${svg("play")}RESUME</button>
        <button class="btn danger" data-cmd="abort">${svg("stop")}ABORT</button>
      </div>
      <div class="ctl-note" id="ctlNote"></div></div>
  </div>

  <div class="grid g4">
    <div class="card"><div class="card-title">${svg("file")}Active Protocol</div><div id="apBody"></div></div>
    <div class="card"><div class="card-title">${svg("clock")}Recent Runs <button class="link" onclick="location.hash='#/history'">View All →</button></div><div id="rrBody" class="list"></div></div>
    <div class="card guide"><div class="title">${svg("book")}User Guide</div>
      <p>Learn how to set up, operate, and monitor CryoAxis with our detailed user guide.</p>
      <div class="btns"><button class="btn" onclick="location.hash='#/guide'">${svg("eye")}View User Guide</button>
      <button class="btn primary" id="dlGuide">${svg("dl")}Download PDF</button></div></div>
    <div class="card"><div class="card-title">${svg("bell")}Alerts &amp; Notifications <button class="link" onclick="location.hash='#/monitor'">View All →</button></div><div id="alBody" class="list"></div></div>
  </div>`;
  document.querySelectorAll("[data-cmd]").forEach((b) => (b.onclick = () => sendCommand(b.dataset.cmd)));
  $("dlGuide").onclick = downloadGuide;
  loadGraph(S.snap && S.snap.run ? S.snap.run.id : null).then(() => {
    charts.home = makeProfileChart("homeChart", S.graph);
    updateGraphLabel();
  });
  loadRecentRuns().then(updateHome);
  refreshAlertHistory().then(updateHome);
  updateHome();
}

function updateGraphLabel() {
  const el = $("gRun");
  if (el) el.textContent = S.graph.run_id ? "· " + S.graph.run_id : "· planned profile (no run recorded yet)";
}

function statusView(snap) {
  if (!snap || !snap.ever_seen) return { cls: "OFFLINE", text: "NO DEVICE" };
  if (!snap.connected) return { cls: "OFFLINE", text: "OFFLINE" };
  return { cls: snap.status, text: snap.status };
}

function updateHome() {
  if (S.page !== "home" || !$("stBadge")) return;
  const snap = S.snap;
  const sv = statusView(snap);
  $("stBadge").className = "status-badge st-" + sv.cls;
  $("stBadge").textContent = sv.text;
  if (!snap) return;
  $("stDevice").textContent = snap.connected ? (snap.device_state || "—") + (snap.device_error && snap.device_error !== "NONE" ? " · " + snap.device_error : "") : "Unavailable";
  $("stStep").textContent = snap.connected && snap.step ? `Step ${snap.step} / ${snap.step_count}${snap.step_name ? " — " + snap.step_name : ""}` : "–";
  $("stProto").textContent = snap.protocol ? `${snap.protocol.name} (v${snap.protocol.version})` : "Not Selected";
  $("stHw").innerHTML = !snap.ever_seen ? '<span class="tag mute">Waiting for device</span>'
    : snap.connected ? '<span class="tag ok">Connected</span>' : `<span class="tag bad">Disconnected</span>`;
  $("connBanner").innerHTML = !snap.ever_seen
    ? '<div class="banner">No telemetry received yet. Power the THULIR controller and check WiFi / server address in <code>Config.h</code>.</div>'
    : !snap.connected ? `<div class="banner err">Hardware disconnected — no telemetry for ${hms(snap.stale_age_s)}. Readings below are unavailable until the device reconnects.</div>` : "";

  const t = snap.temperature;
  $("tActual").innerHTML = isNum(t.actual) ? num(t.actual) + " <small>°C</small>" : "—";
  $("tTarget").innerHTML = isNum(t.target) ? num(t.target) + " <small>°C</small>" : "—";
  $("tDev").textContent = isNum(t.deviation) ? signed(t.deviation) + " °C" : "—";
  $("tRate").textContent = isNum(t.cooling_rate) ? signed(t.cooling_rate) + " °C/min" : "— (insufficient data)";
  const tolText = { within: ["Within tolerance", "ok"], near_limit: ["Approaching limit", "warn"], outside: ["Outside tolerance", "bad"],
    approaching_target: ["Approaching target", "mute"], not_configured: ["Not configured", "mute"], unavailable: ["Unavailable", "mute"] }[t.tolerance_status] || ["—", "mute"];
  $("tTol").innerHTML = `<span class="tag ${tolText[1]}">${tolText[0]}</span>` + (isNum(t.tolerance) ? ` <span class="muted">±${num(t.tolerance, 1)} °C</span>` : "");

  const h = snap.humidity;
  $("hVal").innerHTML = isNum(h.value) ? Math.round(h.value) + " <small>% RH</small>" : "—";
  $("hState").innerHTML = !snap.connected ? '<span class="tag mute">No data</span>' : h.available ? '<span class="tag ok">Live</span>' : '<span class="tag bad">Unavailable</span>';
  $("hTs").textContent = h.timestamp ? shortTime(h.timestamp) + (h.available ? "" : " (stale)") : "—";

  const r = liveRun();
  $("pEl").textContent = r ? hms(r.elapsed_s) : "00:00:00";
  $("pRem").textContent = r && isNum(r.remaining_s) ? hms(r.remaining_s) : (snap.protocol && isNum(snap.protocol.total_duration_s) ? hms(snap.protocol.total_duration_s) : "—");
  const pct = r && isNum(r.progress_pct) ? r.progress_pct : 0;
  $("pBar").style.width = pct + "%";
  $("pPct").textContent = r && isNum(r.progress_pct) ? Math.round(pct) + "%" : "0%";
  $("pTotal").textContent = snap.protocol && isNum(snap.protocol.total_duration_s) ? hms(snap.protocol.total_duration_s) : "—";

  const c = snap.commands;
  document.querySelectorAll("[data-cmd]").forEach((b) => {
    const n = b.dataset.cmd;
    b.disabled = !c[n];
    b.title = c[n] ? "" : (n === "pause" || n === "resume") ? c.pause_note
      : n === "start" && c.start_blocked_reason ? c.start_blocked_reason : "";
  });
  $("ctlNote").textContent = c.pending ? `Waiting for the device to acknowledge '${c.pending.name}'…`
    : (c.start_blocked_reason && snap.status === "READY" ? c.start_blocked_reason : c.channel_note);

  $("apBody").innerHTML = snap.protocol
    ? `<div class="kv"><div class="v">${esc(snap.protocol.name)}</div><div class="muted">Version ${snap.protocol.version} · ${dur(snap.protocol.total_duration_s)}${isNum(snap.protocol.tolerance) ? " · ±" + snap.protocol.tolerance + " °C" : ""}</div></div>
       <div style="margin-top:12px"><button class="btn" onclick="location.hash='#/protocols'">Select Protocol →</button></div>`
    : `<div class="kv"><div class="v">Not Selected</div><div class="muted">–</div></div><div style="margin-top:12px"><button class="btn" onclick="location.hash='#/protocols'">Select Protocol →</button></div>`;

  $("rrBody").innerHTML = S.runs.length ? S.runs.slice(0, 4).map((r2) =>
    `<div class="list-row"><span class="muted mono">${shortDate(r2.started_at)}</span><span class="grow">${esc(r2.protocol_name || "—")}</span><span class="pill ${r2.final_status || "RUNNING"}">${r2.final_status || "Running"}</span></div>`).join("")
    : '<div class="empty">No runs recorded yet.</div>';

  const act = snap.alerts.map((a) => ({ ...a, active: true }));
  const rest = S.alertHistory.filter((a) => a.resolved_at).slice(0, Math.max(0, 4 - act.length)).map((a) => ({ ...a, active: false }));
  const rows = [...act, ...rest].slice(0, 4);
  $("alBody").innerHTML = rows.length ? rows.map(alertRow).join("") : '<div class="empty">No alerts.</div>';
}
function alertRow(a) {
  const k = a.active ? (a.severity === "error" ? "error" : "warning") : "success";
  return `<div class="alert-row"><span class="dot ${k}"></span><span>${esc(a.message)}${a.active ? "" : ' <span class="muted">(resolved)</span>'}</span><span class="ts mono">${shortTime(a.raised_at)}</span></div>`;
}

async function loadRecentRuns() { const r = await api("/api/runs"); if (r.ok) S.runs = r.body; }
async function refreshAlertHistory() { const r = await api("/api/alerts"); if (r.ok) S.alertHistory = r.body.history; }

async function sendCommand(name) {
  if (name === "abort") {
    const ok = await confirmDialog("Abort run?", "The device will be told to stop immediately (the same emergency stop as the keypad). Collected data is kept.", "Abort run");
    if (!ok) return;
  }
  const body = { command: name };
  if (name === "start" && S.snap && S.snap.commands.pin_required) {
    const pin = await promptDialog("Control PIN", "Enter the control PIN to start the device.");
    if (pin === null) return;
    body.pin = pin;
  }
  const r = await api("/api/command", { method: "POST", body });
  const b = r.body || {};
  // 202 = queued for the device; real success/failure arrives as "command_result"
  toast(b.message || (r.ok ? "Command queued." : "Command failed."), r.ok ? "" : "err");
}

function promptDialog(title, message) {
  return new Promise((resolve) => {
    const root = $("modalRoot");
    root.innerHTML = `<div class="modal-back"><div class="modal" role="dialog" aria-modal="true">
      <h3>${esc(title)}</h3><p>${esc(message)}</p>
      <input id="mPin" type="password" autocomplete="off" style="margin-bottom:14px">
      <div class="row"><button class="btn" id="mNo">Cancel</button><button class="btn primary" id="mYes">Send</button></div></div></div>`;
    const done = (v) => { root.innerHTML = ""; resolve(v); };
    $("mNo").onclick = () => done(null);
    $("mYes").onclick = () => done($("mPin").value);
    $("mPin").onkeydown = (e) => { if (e.key === "Enter") done($("mPin").value); };
    $("mPin").focus();
  });
}

function onCommandResult(r) {
  toast(r.ok ? `Device acknowledged '${r.name}': ${r.message}` : `Device did not execute '${r.name}': ${r.message}`, r.ok ? "ok" : "err");
  loadRecentRuns().then(updateHome);
}

function downloadGuide() {
  if (!S.guideAvailable) { toast("User guide PDF will be available soon."); return; }
  location.href = "/userguide.pdf";
}

// ── Run Monitor ──────────────────────────────────────────────────
function pageMonitor() {
  $("view").innerHTML = `
  <div class="page-head"><h1 class="page-title">Run Monitor</h1><span class="spacer"></span>
    <a class="btn" id="mExport" href="#">${svg("dl")}Export to Excel</a></div>
  <div class="grid g4" id="mCards"></div>
  <div class="card" style="margin-bottom:var(--gap)"><div class="card-title">${svg("profile")}Temperature Profile <span class="link muted" id="gRun" style="text-decoration:none"></span></div>
    <div class="chart-wrap tall"><canvas id="monChart"></canvas></div>
    <div class="chart-note">Target = planned protocol profile (time axis starts at run start; approach time is not part of the plan). Actual = logged measurements.</div></div>
  <div class="grid g2">
    <div class="card"><div class="card-title">${svg("bell")}Alerts</div><div id="mAlerts" class="list"></div></div>
    <div class="card"><div class="card-title">${svg("list")}Run Events</div><div class="table-scroll" style="max-height:260px;overflow:auto"><table class="tbl"><thead><tr><th>Time</th><th>Event</th><th>Details</th></tr></thead><tbody id="mEvents"></tbody></table></div></div>
  </div>
  <div class="card" style="margin-bottom:var(--gap)"><div class="card-title">${svg("run")}Latest Measurements</div>
    <div class="table-scroll" style="max-height:300px;overflow:auto"><table class="tbl"><thead><tr><th>Time</th><th>Elapsed</th><th>Step</th><th>Actual °C</th><th>Target °C</th><th>Dev °C</th><th>Rate °C/min</th><th>RH %</th><th>Status</th></tr></thead><tbody id="mSamples"></tbody></table></div></div>
  <div class="card"><div class="card-title">${svg("activity")}Peltier Stage Output</div><div id="mPwm"></div></div>`;
  loadGraph(S.snap && S.snap.run ? S.snap.run.id : null).then(() => {
    charts.mon = makeProfileChart("monChart", S.graph);
    updateGraphLabel();
    updateMonitor();
  });
  refreshAlertHistory().then(updateMonitor);
  loadMonitorTables();
  updateMonitor();
}
let monTablesAt = 0;
async function loadMonitorTables() {
  monTablesAt = Date.now();
  const rid = S.graph.run_id || (S.snap && S.snap.run && S.snap.run.id);
  const body = $("mEvents");
  if (!body) return;
  if (!rid) {
    body.innerHTML = '<tr><td colspan="3" class="empty">No run recorded yet.</td></tr>';
    $("mSamples").innerHTML = '<tr><td colspan="9" class="empty">No measurements recorded yet.</td></tr>';
    return;
  }
  const r = await api("/api/runs/" + encodeURIComponent(rid));
  if (!r.ok || !$("mEvents")) return;
  const ev = r.body.events.slice(-40).reverse();
  $("mEvents").innerHTML = ev.map((e) => `<tr><td class="mono">${shortTime(e.ts)}</td><td>${esc(e.type)}</td><td>${esc(e.message)}</td></tr>`).join("") || '<tr><td colspan="3" class="empty">No events.</td></tr>';
  $("mSamples").innerHTML = r.body.samples.slice(-30).reverse().map((s) =>
    `<tr class="mono"><td>${shortTime(s.ts)}</td><td>${hms(s.elapsed_s)}</td><td>${s.step ?? "—"}</td><td>${num(s.actual, 2)}</td><td>${num(s.target, 2)}</td><td>${signed(s.deviation, 2)}</td><td>${num(s.cooling_rate, 2)}</td><td>${num(s.humidity, 0)}</td><td>${esc(s.status)}</td></tr>`).join("")
    || '<tr><td colspan="9" class="empty">No measurements recorded yet.</td></tr>';
}
function updateMonitor() {
  if (S.page !== "monitor" || !$("mCards") || !S.snap) return;
  const snap = S.snap, t = snap.temperature, r = liveRun(), sv = statusView(snap);
  const rid = S.graph.run_id;
  $("mExport").href = rid ? "/api/runs/" + encodeURIComponent(rid) + "/export" : "#";
  $("mExport").onclick = (e) => { if (!rid) { e.preventDefault(); toast("No run data available to export."); } };
  $("mCards").innerHTML = `
    <div class="card kv"><div class="k">Status</div><div class="v"><span class="status-badge st-${sv.cls}" style="font-size:14px">${sv.text}</span></div><div class="muted">${esc(snap.device_state || "—")}</div></div>
    <div class="card kv"><div class="k">Run</div><div class="v mono">${r ? esc(r.id) : "—"}</div><div class="muted">${r ? (r.active ? "In progress" : esc(r.final_status || "")) : "No active run"}</div></div>
    <div class="card kv"><div class="k">Temperature</div><div class="v mono">${num(t.actual)} / ${num(t.target)} °C</div><div class="muted">Deviation ${isNum(t.deviation) ? signed(t.deviation) + " °C" : "—"} · ${isNum(t.cooling_rate) ? signed(t.cooling_rate) + " °C/min" : "rate n/a"}</div></div>
    <div class="card kv"><div class="k">Progress</div><div class="v mono">${r && isNum(r.progress_pct) ? Math.round(r.progress_pct) + "%" : "—"}</div><div class="muted">${r ? hms(r.elapsed_s) + " elapsed" : "—"}</div></div>`;
  const act = snap.alerts.map((a) => ({ ...a, active: true }));
  const hist = S.alertHistory.filter((a) => a.resolved_at).slice(0, 12).map((a) => ({ ...a, active: false }));
  $("mAlerts").innerHTML = [...act, ...hist].map((a) => `<div class="alert-row"><span class="dot ${a.active ? (a.severity === "error" ? "error" : "warning") : "success"}"></span><span><b>${esc(a.subsystem)}</b> — ${esc(a.message)} <span class="muted">${a.active ? "(active)" : "(resolved " + shortTime(a.resolved_at) + ")"}</span></span><span class="ts mono">${shortTime(a.raised_at)}</span></div>`).join("") || '<div class="empty">No alerts.</div>';
  const bar = (n, v) => `<div class="list-row" style="margin:6px 0"><span style="width:90px">${n}</span><div class="progress" style="flex:1"><i style="width:${isNum(v) ? Math.min(100, Math.max(0, v)) : 0}%"></i></div><b class="mono" style="width:50px;text-align:right">${isNum(v) ? Math.round(v) + "%" : "—"}</b></div>`;
  $("mPwm").innerHTML = bar("PID demand", snap.pidOutput) + bar("Bottom", snap.bottomPWM) + bar("Middle", snap.middlePWM) + bar("Top", snap.topPWM)
    + `<div class="muted" style="margin-top:6px">Servos: ${snap.servos_open === null || snap.servos_open === undefined ? "not reported by firmware" : snap.servos_open ? "open" : "closed"}</div>`;
  if (Date.now() - monTablesAt > 10000) loadMonitorTables();
}

// ── Protocols ────────────────────────────────────────────────────
async function pageProtocols() {
  $("view").innerHTML = '<div id="protoRoot"></div>';
  await reloadProtocols();
}
async function reloadProtocols() {
  const r = await api("/api/protocols");
  if (r.ok) S.protocols = r.body;
  renderProtocols();
}
function renderProtocols() {
  if (S.page !== "protocols") return;
  if (S.editor) return renderEditor();
  const sel = S.snap && S.snap.selected_protocol_id;
  S.shownSelected = sel;
  const rows = S.protocols.map((p) => `<tr class="${p.id === sel ? "sel" : ""}">
    <td><b>${esc(p.name)}</b>${p.id === sel ? ' <span class="tag ok">Selected</span>' : ""}<div class="muted">${esc(p.description || "")}</div></td>
    <td class="mono">${p.step_count}</td><td class="mono">${isNum(p.total_duration_s) ? dur(p.total_duration_s) : "—"}</td>
    <td class="mono">v${p.version}</td><td class="mono">${fullTime(p.modified_at)}</td>
    <td>${p.valid ? '<span class="tag ok">Valid</span>' : `<span class="tag bad" title="${esc(p.errors.join(" "))}">Invalid</span>`}
      <div style="margin-top:4px">${p.device_compatible ? '<span class="tag ok">Device-ready</span>' : p.valid ? `<span class="tag warn" title="${esc((p.device_issues || []).join(" "))}">Not device-compatible</span>` : ""}</div></td>
    <td><div class="acts">
      <button class="btn sm" data-a="view" data-id="${p.id}">View</button>
      <button class="btn sm" data-a="select" data-id="${p.id}" ${p.valid ? "" : "disabled"}>Select</button>
      <button class="btn sm primary" data-a="run" data-id="${p.id}" ${p.device_compatible ? "" : "disabled"} title="${p.device_compatible ? "Select and start on the device" : esc((p.device_issues || ["Invalid protocol"]).join(" "))}">Run</button>
      <button class="btn sm" data-a="edit" data-id="${p.id}">Edit</button>
      <button class="btn sm danger" data-a="delete" data-id="${p.id}">Delete</button></div></td></tr>`).join("");
  $("protoRoot").innerHTML = `<div class="page-head"><h1 class="page-title">Protocols</h1><span class="spacer"></span>
    <button class="btn primary" id="pNew">+ Create New Protocol</button></div>
    <div class="card"><div class="table-scroll"><table class="tbl"><thead><tr><th>Name</th><th>Steps</th><th>Total duration</th><th>Version</th><th>Last modified</th><th>Validation</th><th>Actions</th></tr></thead>
    <tbody>${rows || '<tr><td colspan="7" class="empty">No protocols saved yet.</td></tr>'}</tbody></table></div>
    <div class="chart-note">“Run” sends the protocol’s recipe to the device and starts it, but only if the device acknowledges (the device only supports 4 holds + the −1 °C/min ramp to −20 °C; see the tooltip on “Not device-compatible”). The recipe is applied for that run only — the recipe saved on the device keypad is not overwritten.</div></div>`;
  $("pNew").onclick = () => openEditor("new");
  $("protoRoot").querySelectorAll("[data-a]").forEach((b) => (b.onclick = () => protocolAction(b.dataset.a, Number(b.dataset.id))));
}
async function protocolAction(a, id) {
  const p = S.protocols.find((x) => x.id === id);
  if (!p) return;
  if (a === "view") return openEditor("view", p);
  if (a === "edit") return openEditor("edit", p);
  if (a === "select" || a === "run") {
    const r = await api(`/api/protocols/${id}/select`, { method: "POST" });
    if (!r.ok) { toast((r.body.errors || [r.body.message]).join(" "), "err"); return; }
    toast(`Selected: ${p.name} v${p.version}`, "ok");
    if (a === "run") {
      location.hash = "#/home";
      await sendCommand("start");
    } else reloadProtocols();
  }
  if (a === "delete") {
    if (!(await confirmDialog("Delete protocol?", `“${p.name}” v${p.version} will be permanently deleted. Past runs keep their own copy.`, "Delete"))) return;
    const r = await api(`/api/protocols/${id}`, { method: "DELETE" });
    if (!r.ok) toast(r.body.message, "err"); else { toast("Protocol deleted.", "ok"); reloadProtocols(); }
  }
}

function blankProtocol() {
  return { id: null, name: "", description: "", tolerance: 0.5, version: 1,
    steps: [{ name: "Step 1", type: "hold", target: 25, hold_min: 30 }] };
}
function openEditor(mode, p) {
  S.editor = { mode, id: p ? p.id : null, data: p ? JSON.parse(JSON.stringify(p)) : blankProtocol(), errors: [], valid: null };
  renderEditor();
  validateEditor();
}
function renderEditor() {
  const E = S.editor, d = E.data, ro = E.mode === "view";
  const dis = ro ? "disabled" : "";
  const title = { new: "New Protocol", edit: `Edit Protocol — v${d.version}`, view: "View Protocol" }[E.mode];
  const rows = d.steps.map((s, i) => `<tr>
    <td class="mono">${i + 1}</td>
    <td><input data-f="name" data-i="${i}" value="${esc(s.name)}" ${dis}></td>
    <td><select data-f="type" data-i="${i}" ${dis}><option value="hold" ${s.type === "hold" ? "selected" : ""}>Hold</option><option value="ramp" ${s.type === "ramp" ? "selected" : ""}>Ramp</option></select></td>
    <td><input type="number" step="0.1" data-f="target" data-i="${i}" value="${s.target ?? ""}" ${dis}></td>
    <td>${s.type === "ramp"
      ? `<input type="number" step="0.1" data-f="ramp_rate" data-i="${i}" value="${s.ramp_rate ?? ""}" placeholder="°C/min" ${dis}>`
      : `<input type="number" step="1" data-f="hold_min" data-i="${i}" value="${s.hold_min ?? ""}" placeholder="min" ${dis}>`}</td>
    <td><div class="acts">${ro ? "" : `<button class="btn sm" data-m="up" data-i="${i}" ${i === 0 ? "disabled" : ""}>↑</button><button class="btn sm" data-m="down" data-i="${i}" ${i === d.steps.length - 1 ? "disabled" : ""}>↓</button><button class="btn sm danger" data-m="del" data-i="${i}">Remove</button>`}</div></td></tr>`).join("");
  $("protoRoot").innerHTML = `<div class="page-head"><h1 class="page-title">${title}</h1></div>
  <div class="card">
    <div class="form-grid"><label class="f">Protocol name<input id="eName" value="${esc(d.name)}" ${dis}></label>
      <label class="f">Tolerance ±°C (optional)<input id="eTol" type="number" step="0.1" value="${d.tolerance ?? ""}" ${dis}></label></div>
    <label class="f" style="margin-bottom:12px">Description<textarea id="eDesc" rows="2" ${dis}>${esc(d.description)}</textarea></label>
    <div class="table-scroll"><table class="tbl steps"><thead><tr><th>#</th><th>Step name</th><th>Type</th><th>Target °C</th><th>Duration (min) / Ramp rate (°C/min)</th><th></th></tr></thead><tbody>${rows}</tbody></table></div>
    ${ro ? "" : '<div style="margin-top:8px"><button class="btn sm" id="eAdd">+ Add step</button></div>'}
    <div id="eStatus"></div>
    <div class="row" style="display:flex;gap:8px;justify-content:flex-end;flex-wrap:wrap;margin-top:12px">
      <button class="btn" id="eCancel">${ro ? "Back" : "Cancel"}</button>
      ${ro ? "" : `${E.mode === "edit" ? '<button class="btn" id="eVer">Save as New Version</button>' : ""}<button class="btn primary" id="eSave">Save Protocol</button>`}
    </div></div>`;
  $("eCancel").onclick = () => { S.editor = null; renderProtocols(); };
  if (ro) return;
  $("eName").oninput = (e) => { d.name = e.target.value; validateEditor(); };
  $("eDesc").oninput = (e) => { d.description = e.target.value; };
  $("eTol").oninput = (e) => { d.tolerance = e.target.value === "" ? null : e.target.value; validateEditor(); };
  $("eAdd").onclick = () => { d.steps.push({ name: `Step ${d.steps.length + 1}`, type: "hold", target: 0, hold_min: 10 }); renderEditor(); validateEditor(); };
  $("protoRoot").querySelectorAll("[data-f]").forEach((el) => {
    el.oninput = el.onchange = () => {
      const s = d.steps[Number(el.dataset.i)], f = el.dataset.f;
      s[f] = el.type === "number" ? (el.value === "" ? null : el.value) : el.value;
      if (f === "type") renderEditor();
      validateEditor();
    };
  });
  $("protoRoot").querySelectorAll("[data-m]").forEach((b) => (b.onclick = () => {
    const i = Number(b.dataset.i), m = b.dataset.m;
    if (m === "del") d.steps.splice(i, 1);
    else { const j = m === "up" ? i - 1 : i + 1; [d.steps[i], d.steps[j]] = [d.steps[j], d.steps[i]]; }
    renderEditor(); validateEditor();
  }));
  $("eSave").onclick = () => saveEditor(false);
  if ($("eVer")) $("eVer").onclick = () => saveEditor(true);
}
let valTimer = null;
function validateEditor() {
  clearTimeout(valTimer);
  valTimer = setTimeout(async () => {
    if (!S.editor || !$("eStatus")) return;
    const r = await api("/api/protocols/validate", { method: "POST", body: S.editor.data });
    if (!S.editor || !$("eStatus")) return;
    const b = r.body || {};
    $("eStatus").innerHTML = b.valid ? `<div class="ok-line">✓ Valid — total duration ${dur(b.total_duration_s)}</div>`
      : `<ul class="err-list">${(b.errors || [b.message || "Validation unavailable"]).map((e) => `<li>${esc(e)}</li>`).join("")}</ul>`;
  }, 250);
}
async function saveEditor(asVersion) {
  const E = S.editor;
  let r;
  if (E.mode === "new") r = await api("/api/protocols", { method: "POST", body: E.data });
  else if (asVersion) r = await api(`/api/protocols/${E.id}/version`, { method: "POST", body: E.data });
  else r = await api(`/api/protocols/${E.id}`, { method: "PUT", body: E.data });
  if (!r.ok) {
    const errs = r.body.errors || [r.body.message || "Save failed."];
    $("eStatus").innerHTML = `<ul class="err-list">${errs.map((e) => `<li>${esc(e)}</li>`).join("")}</ul>`;
    toast(errs[0], "err");
    return;
  }
  toast(asVersion ? `Saved as version ${r.body.protocol.version}.` : "Protocol saved.", "ok");
  S.editor = null;
  await reloadProtocols();
}

// ── Run History ──────────────────────────────────────────────────
async function pageHistory() {
  $("view").innerHTML = '<div id="histRoot"></div>';
  S.historyOpen = null;
  await loadRecentRuns();
  renderHistory();
}
function renderHistory() {
  if (S.page !== "history") return;
  const rows = S.runs.map((r) => {
    const secs = r.ended_at ? (new Date(r.ended_at) - new Date(r.started_at)) / 1000 : null;
    return `<tr><td class="mono">${esc(r.id)}</td><td>${esc(r.protocol_name || "—")}${r.protocol_version ? ` <span class="muted">v${r.protocol_version}</span>` : ""}</td>
      <td class="mono">${fullTime(r.started_at)}</td><td class="mono">${fullTime(r.ended_at)}</td><td class="mono">${secs !== null ? hms(secs) : "—"}</td>
      <td><span class="pill ${r.final_status || "RUNNING"}">${r.final_status || "In progress"}</span></td>
      <td class="mono">${r.warnings} / ${r.errors}</td>
      <td><div class="acts"><button class="btn sm" data-open="${esc(r.id)}">Open</button><a class="btn sm" href="/api/runs/${encodeURIComponent(r.id)}/export">Excel</a></div></td></tr>`;
  }).join("");
  $("histRoot").innerHTML = `<div class="page-head"><h1 class="page-title">Run History</h1></div>
    <div class="card"><div class="table-scroll"><table class="tbl"><thead><tr><th>Run ID</th><th>Protocol</th><th>Start</th><th>End</th><th>Duration</th><th>Final status</th><th>Warn / Err</th><th></th></tr></thead>
    <tbody>${rows || '<tr><td colspan="8" class="empty">No runs recorded yet. A run is recorded automatically when the device starts a process.</td></tr>'}</tbody></table></div></div>`;
  $("histRoot").querySelectorAll("[data-open]").forEach((b) => (b.onclick = () => openRun(b.dataset.open)));
}
async function openRun(id) {
  const r = await api("/api/runs/" + encodeURIComponent(id));
  if (!r.ok) { toast(r.body.message || "Could not load run.", "err"); return; }
  const run = r.body;
  const snap = run.protocol_snapshot;
  $("histRoot").innerHTML = `<div class="page-head"><button class="btn" id="hBack">← Back</button><h1 class="page-title">${esc(run.id)}</h1><span class="pill ${run.final_status || "RUNNING"}">${run.final_status || "In progress"}</span><span class="spacer"></span>
    <a class="btn primary" href="/api/runs/${encodeURIComponent(run.id)}/export">${svg("dl")}Export to Excel</a></div>
    <div class="grid g3"><div class="card kv"><div class="k">Protocol</div><div class="v">${esc(run.protocol_name || "—")}${run.protocol_version ? " v" + run.protocol_version : ""}</div></div>
      <div class="card kv"><div class="k">Start</div><div class="v mono">${fullTime(run.started_at)}</div></div>
      <div class="card kv"><div class="k">End</div><div class="v mono">${fullTime(run.ended_at)}</div></div></div>
    <div class="card" style="margin-bottom:var(--gap)"><div class="card-title">${svg("profile")}Temperature Profile</div><div class="chart-wrap tall"><canvas id="histChart"></canvas></div></div>
    <div class="grid g2"><div class="card"><div class="card-title">${svg("list")}Events</div><div class="table-scroll" style="max-height:300px;overflow:auto"><table class="tbl"><tbody>${run.events.map((e) => `<tr><td class="mono">${shortTime(e.ts)}</td><td>${esc(e.type)}</td><td>${esc(e.message)}</td></tr>`).join("") || '<tr><td class="empty">No events.</td></tr>'}</tbody></table></div></div>
    <div class="card"><div class="card-title">${svg("run")}Measurements (${run.samples.length})</div><div class="table-scroll" style="max-height:300px;overflow:auto"><table class="tbl"><thead><tr><th>Elapsed</th><th>Step</th><th>Actual</th><th>Target</th><th>RH</th></tr></thead><tbody>${run.samples.slice(-200).reverse().map((s) => `<tr class="mono"><td>${hms(s.elapsed_s)}</td><td>${s.step ?? "—"}</td><td>${num(s.actual, 2)}</td><td>${num(s.target, 2)}</td><td>${num(s.humidity, 0)}</td></tr>`).join("") || '<tr><td class="empty">No measurements.</td></tr>'}</tbody></table></div></div></div>`;
  $("hBack").onclick = () => { Object.values(charts).forEach((c) => c && c.destroy()); charts = {}; renderHistory(); };
  charts.hist = makeProfileChart("histChart", {
    profile: run.profile, tolerance: snap ? snap.tolerance : null,
    samples: run.samples.map((s) => ({ t: s.elapsed_s, actual: s.actual })),
  });
}

// ── User Guide ───────────────────────────────────────────────────
function pageGuide() {
  $("view").innerHTML = `<div class="guide-page"><h1 class="page-title">User Guide</h1>
    <div class="card guide"><div class="title">${svg("book")}CryoAxis User Guide</div>
    <div class="soon" style="margin-top:12px">The complete user guide is being prepared.<br>Set-up, operation and monitoring instructions will appear here.</div>
    <div class="btns" style="margin-top:14px;grid-template-columns:1fr 1fr">
      <button class="btn" disabled title="Content coming soon">${svg("eye")}View full guide (coming soon)</button>
      <button class="btn primary" id="dlGuide2">${svg("dl")}Download PDF</button></div>
    <p id="guideMsg"></p></div></div>`;
  $("dlGuide2").onclick = () => { downloadGuide(); if (!S.guideAvailable) $("guideMsg").textContent = "User guide PDF will be available soon."; };
}

// ── Settings / audio ─────────────────────────────────────────────
const LANGS = { en: ["English", "en-IN"], ta: ["தமிழ் (Tamil)", "ta-IN"], hi: ["हिन्दी (Hindi)", "hi-IN"] };
const PHRASES = {
  system_ready: { en: "System ready", ta: "அமைப்பு தயாராக உள்ளது", hi: "सिस्टम तैयार है" },
  run_started: { en: "Process started", ta: "செயல்முறை தொடங்கியது", hi: "प्रक्रिया शुरू हुई" },
  step_changed: { en: "Step {n}", ta: "படி {n}", hi: "चरण {n}" },
  paused: { en: "Process paused", ta: "செயல்முறை இடைநிறுத்தப்பட்டது", hi: "प्रक्रिया रोकी गई" },
  resumed: { en: "Process resumed", ta: "செயல்முறை மீண்டும் தொடங்கியது", hi: "प्रक्रिया फिर शुरू हुई" },
  temp_warning: { en: "Temperature warning", ta: "வெப்பநிலை எச்சரிக்கை", hi: "तापमान चेतावनी" },
  completed: { en: "Process completed", ta: "செயல்முறை நிறைவடைந்தது", hi: "प्रक्रिया पूर्ण हुई" },
  aborted: { en: "Process aborted", ta: "செயல்முறை நிறுத்தப்பட்டது", hi: "प्रक्रिया रद्द की गई" },
  critical_error: { en: "Critical system error", ta: "முக்கிய கணினி பிழை", hi: "गंभीर सिस्टम त्रुटि" },
  test: { en: "CryoAxis audio test", ta: "கிரையோஆக்சிஸ் ஒலி சோதனை", hi: "क्रायोएक्सिस ऑडियो परीक्षण" },
};
const spokenIds = new Set();
const lastSpoken = {};
function ttsSupported() { return "speechSynthesis" in window; }
function voiceFor(lang) {
  const vs = ttsSupported() ? speechSynthesis.getVoices() : [];
  const pre = lang.split("-")[0];
  return vs.find((v) => v.lang === lang) || vs.find((v) => v.lang.toLowerCase().startsWith(pre)) || null;
}
function audioStatus() {
  const a = S.settings.audio;
  if (!ttsSupported()) return ["bad", "Speech synthesis is not supported by this browser."];
  if (!a.enabled) return ["mute", "Audio announcements are off."];
  const lang = LANGS[a.language][1];
  if (!voiceFor(lang)) return ["warn", `No ${LANGS[a.language][0]} voice found on this device — the browser default voice will be used and may mispronounce.`];
  return ["ok", "Ready."];
}
function speak(key, vars = {}, force = false) {
  const a = S.settings.audio;
  if ((!a.enabled && !force) || !ttsSupported() || !PHRASES[key]) return;
  let text = PHRASES[key][a.language] || PHRASES[key].en;
  Object.entries(vars).forEach(([k, v]) => (text = text.replace(`{${k}}`, v)));
  const u = new SpeechSynthesisUtterance(text);
  const lang = LANGS[a.language][1];
  u.lang = lang;
  const v = voiceFor(lang);
  if (v) u.voice = v;
  u.volume = a.volume; u.rate = a.rate;
  speechSynthesis.speak(u);
}
function onEvent(ev) {
  if (spokenIds.has(ev.id)) return;
  spokenIds.add(ev.id);
  let det = {};
  try { det = ev.details ? JSON.parse(ev.details) : {}; } catch (_) { /* ignore */ }
  if (det.speak) {
    const now = Date.now();
    if (lastSpoken[det.speak] && now - lastSpoken[det.speak] < 60000 && det.speak !== "step_changed") return;
    lastSpoken[det.speak] = now;
    speak(det.speak, { n: det.step });
  }
  if (ev.type.startsWith("alert_") || ev.type.startsWith("run_")) {
    refreshAlertHistory().then(updateHome);
    if (ev.type !== "alert_raised" && ev.type !== "alert_resolved") loadRecentRuns().then(updateHome);
  }
}
async function saveAudio(patch) {
  Object.assign(S.settings.audio, patch);
  const r = await api("/api/settings", { method: "PUT", body: { audio: patch } });
  if (r.ok) S.settings = r.body; else toast(r.body.message || "Could not save settings.", "err");
  if (S.page === "settings") updateAudioStatus();
}
function updateAudioStatus() {
  const el = $("audioStatus");
  if (!el) return;
  const [k, m] = audioStatus();
  el.innerHTML = `<span class="tag ${k}">${k === "ok" ? "OK" : k === "mute" ? "Off" : k === "warn" ? "Check" : "Unavailable"}</span> ${esc(m)}`;
}
function pageSettings() {
  const a = S.settings.audio;
  $("view").innerHTML = `<h1 class="page-title">Settings</h1>
  <div class="card" style="margin-bottom:var(--gap)"><div class="card-title">Audio &amp; Language</div>
    <div class="setting-row"><span class="name">Audio announcements</span><label class="ctl"><input type="checkbox" id="aOn" ${a.enabled ? "checked" : ""} style="width:auto"> Enabled</label></div>
    <div class="setting-row"><span class="name">Language</span><select class="ctl" id="aLang">${Object.entries(LANGS).map(([k, v]) => `<option value="${k}" ${a.language === k ? "selected" : ""}>${v[0]}</option>`).join("")}</select></div>
    <div class="setting-row"><span class="name">Volume</span><input class="ctl" id="aVol" type="range" min="0" max="1" step="0.05" value="${a.volume}"></div>
    <div class="setting-row"><span class="name">Speech rate</span><input class="ctl" id="aRate" type="range" min="0.5" max="2" step="0.1" value="${a.rate}"></div>
    <div class="setting-row"><span class="name">Test announcement</span><button class="btn" id="aTest">▶ Play test</button></div>
    <div class="setting-row"><span class="name">Audio status</span><span id="audioStatus"></span></div>
    <div class="setting-row"><span class="name">Hardware speaker</span><span class="tag mute">Language sync not supported</span>
      <span class="help">The DFPlayer in the device plays fixed English tracks; firmware v1.0.0 has no command to change its language, so this setting applies to dashboard (browser) audio only. Announcements are spoken by the browser, so a tab must have been interacted with once.</span></div></div>`;
  updateAudioStatus();
  $("aOn").onchange = (e) => saveAudio({ enabled: e.target.checked });
  $("aLang").onchange = (e) => saveAudio({ language: e.target.value });
  $("aVol").onchange = (e) => saveAudio({ volume: Number(e.target.value) });
  $("aRate").onchange = (e) => saveAudio({ rate: Number(e.target.value) });
  $("aTest").onclick = () => { if (!ttsSupported()) return toast("Speech synthesis is not supported by this browser.", "err"); speechSynthesis.cancel(); speak("test", {}, true); };
}
if (ttsSupported()) speechSynthesis.onvoiceschanged = () => { if (S.page === "settings") updateAudioStatus(); };

// ── live data ────────────────────────────────────────────────────
function onSnapshot(snap) {
  const prevRun = S.snap && S.snap.run ? S.snap.run.id : null;
  S.snap = snap;
  S.recvAt = Date.now();
  const lbl = $("connLabel");
  $("connDot").className = "dot " + (snap.connected ? "live" : snap.ever_seen ? "off" : "");
  lbl.textContent = snap.connected ? "LIVE" : snap.ever_seen ? "OFFLINE" : "NO DEVICE";
  const newRun = snap.run ? snap.run.id : null;
  if (newRun && newRun !== prevRun && newRun !== S.graph.run_id) {
    loadGraph(newRun).then(() => { if (S.page === "home" || S.page === "monitor") route(); });
  } else {
    pushLiveSample(snap);
    [charts.home, charts.mon].forEach((c) => c && setActual(c, S.graph.samples));
  }
  updateHome(); updateMonitor();
  if (S.page === "protocols" && !S.editor && snap.selected_protocol_id !== S.shownSelected) renderProtocols();
}
function tickLoop() {
  tickClock();
  if (S.snap && S.snap.run && S.snap.run.active) { updateHome(); }
}

async function boot() {
  tickClock();
  const r = await api("/api/state");
  if (r.ok) {
    S.settings = { ...S.settings, ...r.body.settings };
    S.guideAvailable = r.body.guide_available;
    S.graph.run_id = r.body.graph_run_id;
    S.snap = r.body.snapshot; S.recvAt = Date.now();
  }
  route();
  if (S.snap) onSnapshot(S.snap);
  setInterval(tickLoop, 1000);
  if (typeof io !== "undefined") {
    const socket = io();
    socket.on("connect", () => { S.socketOk = true; });
    socket.on("disconnect", () => { S.socketOk = false; });
    socket.on("snapshot", onSnapshot);
    socket.on("event", onEvent);
    socket.on("command_result", onCommandResult);
  }
  // Fallback / safety poll when the push channel is unavailable (e.g. CDN blocked)
  setInterval(async () => {
    if (S.socketOk) return;
    const s = await api("/api/state");
    if (s.ok) onSnapshot(s.body.snapshot);
  }, 3000);
}
boot();
