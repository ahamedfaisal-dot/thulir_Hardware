const socket = io();

const el = (id) => document.getElementById(id);

const MAX_POINTS = 150;
const chartData = {
  labels: [],
  actual: [],
  target: [],
  setpoint: [],
};

const stabilityData = {
  labels: [],
  stability: [],
  quality: [],
};

let lastMsgTime = 0;
let etaMax = null; // for ETA progress bar

// ── Helpers ─────────────────────────────────────────────────────

function fmtNum(v, digits = 1) {
  if (v === undefined || v === null || Number.isNaN(Number(v))) return "—";
  return Number(v).toFixed(digits);
}

function fmtMs(ms) {
  if (!ms || ms <= 0) return "00:00";
  const totalSec = Math.floor(ms / 1000);
  const h = Math.floor(totalSec / 3600);
  const m = Math.floor((totalSec % 3600) / 60);
  const s = totalSec % 60;
  const pad = (n) => String(n).padStart(2, "0");
  return h > 0 ? `${h}:${pad(m)}:${pad(s)}` : `${pad(m)}:${pad(s)}`;
}

function fmtSec(s) {
  if (s === null || s === undefined) return "—";
  const m = Math.floor(s / 60);
  const sec = s % 60;
  return m > 0 ? `${m}m ${sec}s` : `${sec}s`;
}

function stateClass(state) {
  const active = ["APPROACHING", "HOLDING", "RAMPING", "TRANSITIONING", "COMPLETE"];
  const fault  = ["FAULT", "STOPPED"];
  if (fault.includes(state))  return "state-badge state-fault";
  if (active.includes(state)) return "state-badge state-active";
  return "state-badge state-idle";
}

function scoreClass(score) {
  if (score >= 80) return "accent-teal";
  if (score >= 55) return "accent-amber";
  return "accent-red";
}

function setConnected(isOnline) {
  el("connDot").className  = "dot " + (isOnline ? "dot-online" : "dot-offline");
  el("connLabel").textContent = isOnline ? "LIVE" : "OFFLINE";
}

// ── Main telemetry handler ───────────────────────────────────────

function applyReading(d) {
  lastMsgTime = Date.now();
  setConnected(true);

  // Status strip
  el("stState").textContent = d.state ?? "—";
  el("stState").className   = stateClass(d.state);
  el("stStep").textContent  = `${d.step ?? 0} / 5`;
  el("stError").textContent = d.error ?? "NONE";

  const sensorOk = d.sensorValid === true || d.sensorValid === "true";
  el("stSensor").textContent = sensorOk ? "VALID" : "INVALID";
  el("stSensor").className   = "value tag " + (sensorOk ? "tag-ok" : "tag-bad");

  const rampLag = d.rampLag === true || d.rampLag === "true";
  if (d.state === "RAMPING") {
    el("stRampLag").textContent = rampLag ? `LAG ${fmtNum(d.rampLagAmount)}°C` : "OK";
    el("stRampLag").className   = "value tag " + (rampLag ? "tag-bad" : "tag-ok");
  } else {
    el("stRampLag").textContent = "N/A";
    el("stRampLag").className   = "value tag tag-muted";
  }

  // Servo status (field sent if servo feature active on device)
  if (d.servosOpen !== undefined && d.servosOpen !== null) {
    const open = d.servosOpen === true || d.servosOpen === "true";
    el("stServos").textContent = open ? "OPEN" : "CLOSED";
    el("stServos").className   = "value tag " + (open ? "tag-ok" : "tag-muted");
  }

  // Primary metric cards
  el("mActual").textContent   = fmtNum(d.filteredTemp);
  el("mTarget").textContent   = fmtNum(d.targetTemp);
  el("mSetpoint").textContent = fmtNum(d.setpoint);
  el("mHumidity").textContent = (d.humidityValid === true || d.humidityValid === "true")
    ? fmtNum(d.humidity, 0) : "—";

  // PWM bars
  const pid = Number(d.pidOutput) || 0;
  const bot = Number(d.bottomPWM) || 0;
  const mid = Number(d.middlePWM) || 0;
  const top = Number(d.topPWM)    || 0;

  el("barPID").style.width    = pid + "%";
  el("valPID").textContent    = fmtNum(pid, 0) + "%";
  el("barBottom").style.width = bot + "%";
  el("valBottom").textContent = fmtNum(bot, 0) + "%";
  el("barMiddle").style.width = mid + "%";
  el("valMiddle").textContent = fmtNum(mid, 0) + "%";
  el("barTop").style.width    = top + "%";
  el("valTop").textContent    = fmtNum(top, 0) + "%";

  // Timing
  el("tElapsed").textContent   = fmtMs(Number(d.elapsedMs));
  el("tRemaining").textContent = fmtMs(Number(d.remainingMs));
  el("tUptime").textContent    = fmtMs(Number(d.uptimeMs));

  // ETA to target
  const eta = d.etaToTarget;
  if (eta !== null && eta !== undefined) {
    el("etaVal").textContent = fmtSec(eta);
    if (etaMax === null || eta > etaMax) etaMax = eta;
    const pct = etaMax > 0 ? Math.min(100, (1 - eta / etaMax) * 100) : 0;
    el("etaBar").style.width = pct + "%";
  } else {
    el("etaVal").textContent = "—";
    el("etaBar").style.width = "0%";
  }

  el("lastUpdate").textContent = "Updated " + new Date().toLocaleTimeString();

  // Temperature trend chart
  const t = new Date().toLocaleTimeString();
  chartData.labels.push(t);
  chartData.actual.push(d.filteredTemp);
  chartData.target.push(d.targetTemp);
  chartData.setpoint.push(d.setpoint);
  if (chartData.labels.length > MAX_POINTS) {
    chartData.labels.shift();
    chartData.actual.shift();
    chartData.target.shift();
    chartData.setpoint.shift();
  }
  tempChart.update("none");

  // ── ML / Seed Intelligence ──────────────────────────────────────

  // Viability score
  const viability = d.seedViabilityScore;
  if (viability !== undefined && viability !== null) {
    el("mlViability").textContent  = fmtNum(viability, 1);
    el("mlViability").className    = "metric-value mono " + scoreClass(viability);
    el("mlViabilityBar").style.width = viability + "%";
    el("mlViabilityBar").className = "ml-bar-fill " + scoreBarClass(viability);
  }

  // Germination rate
  const germ = d.germRatePct;
  if (germ !== undefined && germ !== null) {
    el("mlGerm").textContent      = fmtNum(germ, 1);
    el("mlGerm").className        = "metric-value mono " + scoreClass(germ);
    el("mlGermBar").style.width   = germ + "%";
    el("mlGermBar").className     = "ml-bar-fill " + scoreBarClass(germ);
  }

  // Life extension
  if (d.lifeExtensionYears !== undefined && d.lifeExtensionYears !== null) {
    el("mlLifeYears").textContent  = "+" + fmtNum(d.lifeExtensionYears, 1);
    el("mlLifeFactor").textContent = fmtNum(d.lifeExtensionFactor, 1) + "× storage factor";
  }

  // Process quality
  const quality = d.processQualityScore;
  if (quality !== undefined && quality !== null) {
    el("mlQuality").textContent      = fmtNum(quality, 1);
    el("mlQuality").className        = "metric-value mono " + scoreClass(quality);
    el("mlQualityBar").style.width   = quality + "%";
    el("mlQualityBar").className     = "ml-bar-fill " + scoreBarClass(quality);
  }

  // Environmental analysis
  if (d.dewPoint !== undefined && d.dewPoint !== null) {
    const dp   = Number(d.dewPoint);
    const temp = Number(d.filteredTemp);
    el("envDewPoint").textContent = fmtNum(dp, 1) + " °C";
    // Condensation risk: dew point near or above surface temp
    const margin = temp - dp;
    let dpStatus, dpClass;
    if (margin < 1)       { dpStatus = "HIGH RISK";   dpClass = "tag-bad"; }
    else if (margin < 5)  { dpStatus = "MODERATE";    dpClass = "tag-warn"; }
    else                  { dpStatus = "LOW RISK";     dpClass = "tag-ok"; }
    el("envDewStatus").textContent = dpStatus;
    el("envDewStatus").className   = "tag " + dpClass;

    // Condensation risk in science panel
    el("sciCondRisk").textContent  = dpStatus;
    el("sciCondRisk").className    = "science-val " + (dpClass === "tag-bad" ? "accent-red" : dpClass === "tag-warn" ? "accent-amber" : "accent-teal");
  }

  if (d.tempStabilityIndex !== undefined && d.tempStabilityIndex !== null) {
    const si = Number(d.tempStabilityIndex);
    el("envStability").textContent      = fmtNum(si, 1);
    el("envStabilityBar").style.width   = si + "%";
    el("envStabilityBar").className     = "pwm-fill " + (si >= 80 ? "fill-teal" : si >= 50 ? "fill-mid" : "fill-warn");
  }

  if (d.inBandRatio !== undefined && d.inBandRatio !== null) {
    const adh = Number(d.inBandRatio);
    el("envAdherence").textContent    = fmtNum(adh, 1) + "%";
    el("envAdherenceBar").style.width = adh + "%";
  }

  // Environmental advice note
  const rh  = Number(d.humidity);
  const tmp = Number(d.filteredTemp);
  let note = "";
  if (!isNaN(rh) && rh > 25)  note += "⚠ Humidity >25% may reduce seed longevity. ";
  if (!isNaN(tmp) && tmp > 5)  note += "ℹ Temperature above 5°C — sub-zero storage extends life further. ";
  if (!isNaN(tmp) && tmp <= -15) note += "✅ Optimal cryogenic range for long-term seed storage. ";
  el("envNote").textContent = note;

  // Science panel
  if (d.filteredTemp !== undefined && d.filteredTemp !== null) {
    el("sciAvgTemp").textContent = fmtNum(d.filteredTemp) + " °C";
  }
  if (d.lifeExtensionFactor !== undefined && d.lifeExtensionFactor !== null) {
    el("sciQ10").textContent = fmtNum(d.lifeExtensionFactor, 1) + "×";
  }

  // Predicted viable until
  if (d.lifeExtensionYears !== undefined && d.lifeExtensionYears !== null) {
    const years = Number(d.lifeExtensionYears);
    const until = new Date();
    until.setFullYear(until.getFullYear() + Math.round(2 + years));
    el("sciViableUntil").textContent = until.getFullYear() + " (est.)";
  }

  // Stability + Quality mini-chart
  stabilityData.labels.push(t);
  stabilityData.stability.push(d.tempStabilityIndex ?? null);
  stabilityData.quality.push(d.processQualityScore ?? null);
  if (stabilityData.labels.length > MAX_POINTS) {
    stabilityData.labels.shift();
    stabilityData.stability.shift();
    stabilityData.quality.shift();
  }
  stabilityChart.update("none");
}

function scoreBarClass(score) {
  if (score >= 80) return "fill-teal";
  if (score >= 55) return "fill-mid";
  return "fill-warn";
}

// ── Socket events ────────────────────────────────────────────────

socket.on("connect",    () => setConnected(true));
socket.on("disconnect", () => setConnected(false));
socket.on("telemetry",  applyReading);

setInterval(() => {
  if (lastMsgTime && Date.now() - lastMsgTime > 8000) {
    setConnected(false);
  }
}, 2000);

fetch("/api/latest").then(r => r.json()).then(d => {
  if (d && Object.keys(d).length) applyReading(d);
});

// ── Temperature Trend Chart ──────────────────────────────────────

const tempCtx = document.getElementById("tempChart").getContext("2d");
const tempChart = new Chart(tempCtx, {
  type: "line",
  data: {
    labels: chartData.labels,
    datasets: [
      {
        label: "Actual",
        data: chartData.actual,
        borderColor: "#0f2f4f",
        backgroundColor: "transparent",
        borderWidth: 2,
        pointRadius: 0,
        tension: 0.15,
      },
      {
        label: "Target",
        data: chartData.target,
        borderColor: "#b4680a",
        backgroundColor: "transparent",
        borderWidth: 1.5,
        borderDash: [5, 3],
        pointRadius: 0,
        tension: 0,
      },
      {
        label: "Setpoint",
        data: chartData.setpoint,
        borderColor: "#67b3d8",
        backgroundColor: "transparent",
        borderWidth: 1.5,
        pointRadius: 0,
        tension: 0.1,
      },
    ],
  },
  options: {
    animation: false,
    responsive: true,
    interaction: { mode: "index", intersect: false },
    scales: {
      x: { ticks: { maxTicksLimit: 8, color: "#5c6b7a" }, grid: { color: "#eef1f4" } },
      y: { ticks: { color: "#5c6b7a" }, grid: { color: "#eef1f4" }, title: { display: true, text: "°C" } },
    },
    plugins: {
      legend: { position: "top", labels: { boxWidth: 12, font: { size: 11 } } },
    },
  },
});

// ── Stability + Quality Chart ────────────────────────────────────

const stabCtx = document.getElementById("stabilityChart").getContext("2d");
const stabilityChart = new Chart(stabCtx, {
  type: "line",
  data: {
    labels: stabilityData.labels,
    datasets: [
      {
        label: "Temp Stability Index",
        data: stabilityData.stability,
        borderColor: "#0e7c72",
        backgroundColor: "rgba(14,124,114,0.08)",
        borderWidth: 2,
        pointRadius: 0,
        tension: 0.2,
        fill: true,
      },
      {
        label: "Process Quality Score",
        data: stabilityData.quality,
        borderColor: "#0f2f4f",
        backgroundColor: "transparent",
        borderWidth: 1.5,
        borderDash: [4, 3],
        pointRadius: 0,
        tension: 0.2,
      },
    ],
  },
  options: {
    animation: false,
    responsive: true,
    interaction: { mode: "index", intersect: false },
    scales: {
      x: { ticks: { maxTicksLimit: 8, color: "#5c6b7a" }, grid: { color: "#eef1f4" } },
      y: {
        min: 0, max: 100,
        ticks: { color: "#5c6b7a" },
        grid: { color: "#eef1f4" },
        title: { display: true, text: "Score (0–100)" },
      },
    },
    plugins: {
      legend: { position: "top", labels: { boxWidth: 12, font: { size: 11 } } },
    },
  },
});
