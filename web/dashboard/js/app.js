import { diffSchematicTexts, getEngine } from "./wasm.js";
import {
  clearHistory,
  loadHistory,
  makeHistoryId,
  saveHistoryEntry,
} from "./history.js";
import {
  filterChanges,
  groupChanges,
  renderChangeList,
  renderInspector,
  renderKpis,
  toMarkdown,
} from "./render.js";

const state = {
  beforeFile: null,
  afterFile: null,
  diff: null,
  labels: { before: "before", after: "after" },
  significance: "significant",
  query: "",
  selectedIndex: 0,
  selectedChange: null,
  source: null,
};

const els = {
  enginePill: document.getElementById("engine-pill"),
  status: document.getElementById("status"),
  beforeName: document.getElementById("before-name"),
  afterName: document.getElementById("after-name"),
  beforeInput: document.getElementById("before"),
  afterInput: document.getElementById("after"),
  dropBefore: document.getElementById("drop-before"),
  dropAfter: document.getElementById("drop-after"),
  btnRun: document.getElementById("btn-run"),
  btnSample: document.getElementById("btn-sample"),
  btnExportMd: document.getElementById("btn-export-md"),
  btnExportJson: document.getElementById("btn-export-json"),
  gatePanel: document.getElementById("gate-panel"),
  gateBanner: document.getElementById("gate-banner"),
  gateLabel: document.getElementById("gate-label"),
  gateMeta: document.getElementById("gate-meta"),
  kpiRow: document.getElementById("kpi-row"),
  resultsPanel: document.getElementById("results-panel"),
  emptyHint: document.getElementById("empty-hint"),
  filters: document.getElementById("filters"),
  search: document.getElementById("search"),
  changeList: document.getElementById("change-list"),
  inspector: document.getElementById("inspector"),
  historyList: document.getElementById("history-list"),
  btnClearHistory: document.getElementById("btn-clear-history"),
  viewTitle: document.getElementById("view-title"),
  viewSub: document.getElementById("view-sub"),
};

const VIEW_META = {
  workspace: {
    title: "Workspace",
    sub: "Compare two KiCad schematics by electrical connectivity",
  },
  history: {
    title: "History",
    sub: "Re-open diffs you ran in this browser",
  },
  help: {
    title: "Guide",
    sub: "How this dashboard fits next to the CLI and CI",
  },
};

function setStatus(text) {
  els.status.textContent = text;
}

function refreshRunEnabled() {
  els.btnRun.disabled = !(state.beforeFile && state.afterFile);
}

function showView(name) {
  document.querySelectorAll(".nav-btn").forEach((btn) => {
    btn.classList.toggle("is-active", btn.dataset.view === name);
  });
  document.querySelectorAll(".view").forEach((view) => {
    const match = view.id === `view-${name}`;
    view.hidden = !match;
    view.classList.toggle("is-visible", match);
  });
  const meta = VIEW_META[name] || VIEW_META.workspace;
  els.viewTitle.textContent = meta.title;
  els.viewSub.textContent = meta.sub;
  if (name === "history") {
    renderHistory();
  }
}

function wireNav() {
  document.querySelectorAll(".nav-btn").forEach((btn) => {
    btn.addEventListener("click", () => showView(btn.dataset.view));
  });
}

function wireDrops() {
  bindDrop(els.dropBefore, els.beforeInput, (file) => {
    state.beforeFile = file;
    els.beforeName.textContent = file ? file.name : "Drop or choose .kicad_sch";
    refreshRunEnabled();
  });
  bindDrop(els.dropAfter, els.afterInput, (file) => {
    state.afterFile = file;
    els.afterName.textContent = file ? file.name : "Drop or choose .kicad_sch";
    refreshRunEnabled();
  });
}

function bindDrop(dropEl, inputEl, onFile) {
  dropEl.addEventListener("click", () => inputEl.click());
  dropEl.addEventListener("keydown", (event) => {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      inputEl.click();
    }
  });
  inputEl.addEventListener("change", () => {
    onFile(inputEl.files[0] || null);
  });
  ["dragenter", "dragover"].forEach((name) => {
    dropEl.addEventListener(name, (event) => {
      event.preventDefault();
      dropEl.classList.add("is-drag");
    });
  });
  ["dragleave", "drop"].forEach((name) => {
    dropEl.addEventListener(name, (event) => {
      event.preventDefault();
      dropEl.classList.remove("is-drag");
    });
  });
  dropEl.addEventListener("drop", (event) => {
    const file = event.dataTransfer.files[0];
    if (file) onFile(file);
  });
}

function wireFilters() {
  const modes = [
    ["significant", "Significant"],
    ["cosmetic", "Cosmetic"],
    ["all", "All"],
  ];
  els.filters.innerHTML = modes
    .map(
      ([id, label]) =>
        `<button type="button" class="chip${
          state.significance === id ? " is-on" : ""
        }" data-mode="${id}">${label}</button>`
    )
    .join("");
  els.filters.querySelectorAll(".chip").forEach((chip) => {
    chip.addEventListener("click", () => {
      state.significance = chip.dataset.mode;
      els.filters.querySelectorAll(".chip").forEach((c) => {
        c.classList.toggle("is-on", c.dataset.mode === state.significance);
      });
      refreshResultsList();
    });
  });
  els.search.addEventListener("input", () => {
    state.query = els.search.value;
    refreshResultsList();
  });
}

function applyDiff(diff, labels, source) {
  state.diff = diff;
  state.labels = labels;
  state.source = source;
  state.selectedIndex = 0;
  state.query = "";
  els.search.value = "";

  const summary = diff.summary || {};
  const gate = summary.gate || "?";
  els.gatePanel.hidden = false;
  els.resultsPanel.hidden = false;
  els.emptyHint.hidden = true;
  els.gateBanner.dataset.gate = gate;
  els.gateLabel.textContent = gate === "PASS" ? "Gate PASS" : gate === "FAIL" ? "Gate FAIL" : gate;
  els.gateMeta.textContent = `${labels.before} → ${labels.after}`;
  renderKpis(els.kpiRow, diff);
  els.btnExportMd.disabled = false;
  els.btnExportJson.disabled = false;
  refreshResultsList();
  showView("workspace");
}

function refreshResultsList() {
  if (!state.diff) return;
  const filtered = filterChanges(state.diff, {
    significance: state.significance,
    query: state.query,
  });
  const groups = groupChanges(filtered);
  const flat = groups.flatMap((g) => g.items);
  if (!flat.length) {
    state.selectedChange = null;
    state.selectedIndex = -1;
  } else if (state.selectedIndex < 0 || state.selectedIndex >= flat.length) {
    state.selectedIndex = 0;
    state.selectedChange = flat[0];
  } else {
    state.selectedChange = flat[state.selectedIndex];
  }
  renderChangeList(els.changeList, groups, state.selectedIndex, (idx, change) => {
    state.selectedIndex = idx;
    state.selectedChange = change;
    els.changeList.querySelectorAll(".change-item").forEach((el, i) => {
      el.classList.toggle("is-selected", i === idx);
    });
    renderInspector(els.inspector, change);
  });
  renderInspector(els.inspector, state.selectedChange);
}

async function runDiff() {
  if (!state.beforeFile || !state.afterFile) return;
  els.btnRun.disabled = true;
  setStatus("Diffing in WebAssembly…");
  try {
    const beforeText = await state.beforeFile.text();
    const afterText = await state.afterFile.text();
    const diff = await diffSchematicTexts(beforeText, afterText);
    const labels = {
      before: state.beforeFile.name,
      after: state.afterFile.name,
    };
    applyDiff(diff, labels, "wasm");
    saveHistoryEntry({
      id: makeHistoryId(),
      at: new Date().toISOString(),
      labels,
      summary: diff.summary || {},
      diff,
    });
    setStatus("Done — computation stayed in this browser");
  } catch (err) {
    setStatus(String(err.message || err));
    els.emptyHint.hidden = false;
    els.gatePanel.hidden = true;
    els.resultsPanel.hidden = true;
  } finally {
    refreshRunEnabled();
  }
}

async function loadSample() {
  setStatus("Loading sample DiffResult…");
  try {
    const url = new URL("../data/sample-diff.json", import.meta.url);
    const response = await fetch(url);
    if (!response.ok) {
      throw new Error(
        "Could not fetch sample JSON (serve the repo root or web/ so /tests is reachable). Using embedded fallback."
      );
    }
    const diff = await response.json();
    applySample(diff);
  } catch (_) {
    applySample(embeddedSample());
  }
}

function applySample(diff) {
  const labels = {
    before: diff.before_ref || "sample-before",
    after: diff.after_ref || "sample-after",
  };
  applyDiff(diff, labels, "sample");
  saveHistoryEntry({
    id: makeHistoryId(),
    at: new Date().toISOString(),
    labels,
    summary: diff.summary || {},
    diff,
  });
  setStatus("Sample DiffResult loaded (UI walkthrough)");
}

function renderHistory() {
  const items = loadHistory();
  if (!items.length) {
    els.historyList.innerHTML =
      `<p class="muted">No runs yet. Diff a pair in Workspace or load a sample.</p>`;
    return;
  }
  els.historyList.innerHTML = "";
  for (const item of items) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "history-item";
    const gate = item.summary?.gate || "?";
    const sig = item.summary?.significant_count ?? "?";
    btn.innerHTML =
      `<strong>${gate}</strong> · ${sig} significant` +
      `<span class="history-meta">${escapeText(item.labels?.before || "?")} → ${escapeText(
        item.labels?.after || "?"
      )}</span>` +
      `<span class="history-meta">${new Date(item.at).toLocaleString()}</span>`;
    btn.addEventListener("click", () => {
      applyDiff(item.diff, item.labels, "history");
      setStatus("Restored from local history");
    });
    els.historyList.appendChild(btn);
  }
}

function download(filename, text, type) {
  const blob = new Blob([text], { type });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

function escapeText(value) {
  return String(value);
}

function embeddedSample() {
  return {
    schema_version: "1.0",
    before_ref: "sample-before",
    after_ref: "sample-after",
    summary: {
      significant_count: 3,
      cosmetic_count: 1,
      gate: "FAIL",
      by_type: {
        ComponentModified: 1,
        PinConnectionChanged: 1,
        NetRenamed: 1,
        NetAdded: 1,
      },
    },
    changes: [
      {
        type: "ComponentModified",
        significance: "SIGNIFICANT",
        critical: false,
        message: "R5: value 10k -> 4k7",
        payload: {
          ref: "R5",
          changes: [{ field: "value", before: "10k", after: "4k7" }],
        },
      },
      {
        type: "PinConnectionChanged",
        significance: "SIGNIFICANT",
        critical: false,
        message: "U3.7 moved from GND to +3V3",
        payload: { pin_id: "U3.7", before_net: "GND", after_net: "+3V3" },
      },
      {
        type: "NetAdded",
        significance: "SIGNIFICANT",
        critical: false,
        message: "Net I2C_SCL added",
        payload: { net: { name: "I2C_SCL", pins: ["U1.12"] } },
      },
      {
        type: "NetRenamed",
        significance: "COSMETIC",
        critical: false,
        message: "Net renamed: Net-(R4-Pad2) -> Net-(R4-Pad3)",
        payload: {
          before_name: "Net-(R4-Pad2)",
          after_name: "Net-(R4-Pad3)",
        },
      },
    ],
  };
}

async function boot() {
  wireNav();
  wireDrops();
  wireFilters();

  els.btnRun.addEventListener("click", () => {
    runDiff().catch((err) => setStatus(String(err)));
  });
  els.btnSample.addEventListener("click", () => {
    loadSample().catch((err) => setStatus(String(err)));
  });
  els.btnExportMd.addEventListener("click", () => {
    if (!state.diff) return;
    download("netdiff-report.md", toMarkdown(state.diff, state.labels), "text/markdown");
  });
  els.btnExportJson.addEventListener("click", () => {
    if (!state.diff) return;
    download(
      "netdiff-result.json",
      JSON.stringify(state.diff, null, 2),
      "application/json"
    );
  });
  els.btnClearHistory.addEventListener("click", () => {
    clearHistory();
    renderHistory();
  });

  try {
    const { version } = await getEngine();
    els.enginePill.dataset.state = "ready";
    els.enginePill.textContent = `Engine ${version}`;
    setStatus("Engine ready — choose schematics or load a sample");
  } catch (err) {
    els.enginePill.dataset.state = "error";
    els.enginePill.textContent = "Engine offline";
    setStatus(String(err.message || err) + " — Sample result still works.");
  }
}

boot();
