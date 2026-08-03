/** Load and call the NetDiff Emscripten module (shared with web/demo). */

let modulePromise = null;

export function getEngine() {
  if (!modulePromise) {
    modulePromise = loadModule();
  }
  return modulePromise;
}

async function loadModule() {
  if (typeof createNetdiffModule !== "function") {
    throw new Error(
      "WASM glue missing. Build with emcmake and ensure web/demo/wasm/netdiff.js is present."
    );
  }
  const Module = await createNetdiffModule({
    locateFile(path) {
      if (path.endsWith(".wasm")) {
        return new URL("../../demo/wasm/netdiff.wasm", import.meta.url).href;
      }
      return path;
    },
  });
  const version = Module.ccall("netdiff_version", "string", [], []);
  return { Module, version };
}

function ensureDir(Module, path) {
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
}

export async function diffSchematicTexts(beforeText, afterText) {
  const { Module } = await getEngine();
  try {
    Module.FS.mkdir("/before");
  } catch (_) {}
  try {
    Module.FS.mkdir("/after");
  } catch (_) {}

  const beforePath = "/before/schematic.kicad_sch";
  const afterPath = "/after/schematic.kicad_sch";
  ensureDir(Module, beforePath);
  ensureDir(Module, afterPath);
  Module.FS.writeFile(beforePath, beforeText);
  Module.FS.writeFile(afterPath, afterText);

  const ptr = Module.ccall(
    "netdiff_diff_paths",
    "number",
    ["string", "string"],
    [beforePath, afterPath]
  );
  const json = Module.UTF8ToString(ptr);
  Module.ccall("netdiff_free", null, ["number"], [ptr]);
  const data = JSON.parse(json);
  if (data.error) {
    throw new Error(data.error);
  }
  return data;
}
