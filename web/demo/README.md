# NetDiff WASM browser demo (T2.5)

Static page that loads `libnetdiff` via Emscripten and diffs two `.kicad_sch` files
**entirely in the browser** (no upload).

## Build the module

```bash
# once: install emsdk and activate emcmake/emcc
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --target netdiff_wasm
```

Artifacts are copied to `web/demo/wasm/netdiff.js` + `netdiff.wasm`.

## Serve locally

```bash
python -m http.server 8080 --directory web/demo
# open http://localhost:8080
```

## Match the native CLI

```bash
python tests/wasm/compare_wasm_to_cli.py \
  --cli build/cli/netdiff \
  --wasm-js web/demo/wasm/netdiff.js \
  --before tests/corpus/ecc83/ecc83-pp.kicad_sch \
  --after tests/corpus/ecc83/ecc83-pp.kicad_sch
```

Requires Node.js to instantiate the WASM module.

## Vercel

Deploy the whole static site from **`web/`** (dashboard + demo). See `web/vercel.json`.

Legacy: a project pointed only at `web/demo` still works; prefer the shared `web` root so `/` is the dashboard.
