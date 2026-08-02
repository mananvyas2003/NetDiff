/* NetDiff browser demo — all computation stays client-side (T2.5). */

let modulePromise = null;
let beforeFile = null;
let afterFile = null;

const statusEl = document.getElementById("status");
const runBtn = document.getElementById("run");
const results = document.getElementById("results");
const summaryEl = document.getElementById("summary");
const outputEl = document.getElementById("output");
const dropzone = document.getElementById("dropzone");

function setStatus(text) {
  statusEl.textContent = text;
}

function refreshReady() {
  runBtn.disabled = !(modulePromise && beforeFile && afterFile);
}

async function loadModule() {
  if (typeof createNetdiffModule !== "function") {
    throw new Error("wasm/netdiff.js missing — build with emcmake (see web/demo/README.md)");
  }
  const Module = await createNetdiffModule();
  setStatus(`WASM ready (netdiff ${Module.ccall("netdiff_version", "string", [], [])})`);
  return Module;
}

function readFile(file) {
  return file.text();
}

function writeVirtual(Module, path, text) {
  const parts = path.split("/").filter(Boolean);
  let cur = "";
  for (let i = 0; i < parts.length - 1; i++) {
    cur += "/" + parts[i];
    try {
      Module.FS.mkdir(cur);
    } catch (_) {
      /* exists */
    }
  }
  Module.FS.writeFile(path, text);
}

async function runDiff() {
  results.hidden = true;
  setStatus("Diffing…");
  const Module = await modulePromise;
  const beforeText = await readFile(beforeFile);
  const afterText = await readFile(afterFile);

  try {
    Module.FS.mkdir("/before");
  } catch (_) {}
  try {
    Module.FS.mkdir("/after");
  } catch (_) {}

  const beforePath = "/before/schematic.kicad_sch";
  const afterPath = "/after/schematic.kicad_sch";
  writeVirtual(Module, beforePath, beforeText);
  writeVirtual(Module, afterPath, afterText);

  const ptr = Module.ccall(
    "netdiff_diff_paths",
    "number",
    ["string", "string"],
    [beforePath, afterPath]
  );
  const json = Module.UTF8ToString(ptr);
  Module.ccall("netdiff_free", null, ["number"], [ptr]);

  let data;
  try {
    data = JSON.parse(json);
  } catch (err) {
    setStatus("Invalid JSON from WASM");
    outputEl.textContent = json;
    results.hidden = false;
    return;
  }

  if (data.error) {
    summaryEl.textContent = "ERROR";
    outputEl.textContent = data.error;
  } else {
    const s = data.summary || {};
    summaryEl.textContent =
      `gate=${s.gate}  significant=${s.significant_count}  cosmetic=${s.cosmetic_count}`;
    const changes = (data.changes || [])
      .filter((c) => c.significance === "SIGNIFICANT")
      .map((c) => `- [${c.type}] ${c.message}`)
      .join("\n");
    outputEl.textContent = changes || "No electrical changes.";
  }
  results.hidden = false;
  setStatus("Done (client-side only)");
}

function assignFiles(files) {
  const list = Array.from(files || []).filter((f) => /\.kicad_sch$/i.test(f.name) || true);
  if (list.length >= 2) {
    beforeFile = list[0];
    afterFile = list[1];
    document.getElementById("before").value = "";
    document.getElementById("after").value = "";
    setStatus(`Loaded ${beforeFile.name} → ${afterFile.name}`);
  }
  refreshReady();
}

document.getElementById("before").addEventListener("change", (e) => {
  beforeFile = e.target.files[0] || null;
  refreshReady();
});
document.getElementById("after").addEventListener("change", (e) => {
  afterFile = e.target.files[0] || null;
  refreshReady();
});
runBtn.addEventListener("click", () => {
  runDiff().catch((err) => {
    setStatus(String(err));
  });
});

["dragenter", "dragover"].forEach((ev) => {
  dropzone.addEventListener(ev, (e) => {
    e.preventDefault();
    dropzone.classList.add("drag");
  });
});
["dragleave", "drop"].forEach((ev) => {
  dropzone.addEventListener(ev, (e) => {
    e.preventDefault();
    dropzone.classList.remove("drag");
  });
});
dropzone.addEventListener("drop", (e) => {
  assignFiles(e.dataTransfer.files);
});

modulePromise = loadModule()
  .then((m) => {
    refreshReady();
    return m;
  })
  .catch((err) => {
    setStatus(String(err));
    modulePromise = null;
    return null;
  });
