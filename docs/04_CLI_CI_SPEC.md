# 04 — CLI, CI & Plugin Specification

## 1. CLI surface (`netdiff`)

### 1.1 Commands
```
netdiff diff <before> <after> [options]     # diff two .kicad_sch project entries (or dirs)
netdiff diff --git <ref_a> <ref_b> [path]   # diff two git revisions of a project
netdiff diff --staged [path]                # working tree vs HEAD (default path = cwd)
netdiff graph <schematic> [options]         # emit the ConnectivityGraph (netlist) as JSON
netdiff validate [path]                     # sanity-check a project parses & resolves
netdiff version
```

### 1.2 Options
```
--format <text|json|markdown|sarif>   default: text
--output <file>                       default: stdout
--config <path>                       default: ./.netdiff.yml if present
--fail-on <significant|any|never>     override config gate
--no-color                            plain text output
--quiet                               errors only
--include-cosmetic                    show cosmetic changes too (default: hidden in text)
```

### 1.3 Exit codes (contract — CI depends on these)
```
0   diff completed, gate PASS (no gating changes)
1   diff completed, gate FAIL (gating changes found)   ← CI should fail the build
2   usage error (bad args)
3   parse/resolve error on an input (malformed schematic)
4   internal error
```
Never exit 0 on a parse error. Never crash; map every failure to a code and a clear stderr message.

### 1.4 Output formats
- **text:** colorized, human summary + grouped changes. Cosmetic hidden unless `--include-cosmetic`.
- **json:** the full `DiffResult` (schema in `02_DATA_MODEL.md`). For tooling.
- **markdown:** compact table + bullets, designed to be posted as a PR comment.
- **sarif:** SARIF 2.1.0 so results appear in GitHub/GitLab code-scanning UIs. Each significant
  change is a `result` with a `ruleId` (e.g. `pin-connection-changed`), a `level`
  (`error`/`warning`/`note` from significance), and a location (schematic file + component/net).

### 1.5 Git integration details
- `--git a b [path]`: check out (or `git show`) each revision's schematic files into temp dirs,
  build both graphs, diff. Must handle multi-file hierarchical projects (fetch all sheet files at
  each revision, not just the entry file).
- `--staged`: compare working-tree files against their `HEAD` blobs.
- Register as a git difftool optionally: `git config diff.netdiff...` (document, don't require).

## 2. CI integrations

### 2.1 GitHub Action (`ci/action.yml` + `ci/README`)
Behavior:
- On pull_request, run `netdiff diff --git <base_sha> <head_sha>`.
- Produce markdown → post/update a single PR comment (idempotent; edit the existing comment).
- Produce sarif → upload so changes show in the "Files changed"/checks surface.
- Exit non-zero (fail the check) when gate FAILs, unless the repo sets `fail_on: never`.
Inputs: `path`, `config`, `fail-on`, `comment` (bool), `sarif` (bool).

### 2.2 GitLab CI template (`ci/gitlab-netdiff.yml`)
- A job that runs the CLI on merge requests, writes the report as an MR note and a CI artifact,
  and sets the job status from the exit code.

### 2.3 pre-commit hook (`ci/.pre-commit-hooks.yaml`)
- A hook that runs `netdiff diff --staged` and blocks the commit on significant changes (developer
  can `--no-verify` to bypass). Ships as an installable pre-commit repo.

## 3. KiCad plugin

### 3.1 Scope
- An Action plugin, distributed via KiCad's Plugin & Content Manager.
- Button: "NetDiff: review my changes." Runs `netdiff diff --staged` for the open project, parses the
  JSON, and shows a panel listing significant changes grouped by type, each clickable to navigate to
  the affected component/net in the editor (use stored `position` metadata for navigation).

### 3.2 Constraints
- The plugin **shells out to the `netdiff` binary**; it does not reimplement connectivity or diff in
  Python. It bundles or locates the binary.
- Works offline. No network calls. (Privacy is a selling point.)
- Degrade gracefully if the project isn't a git repo (offer file-vs-file picker instead).

## 4. Configuration resolution order
1. `--config <path>` if given.
2. `./.netdiff.yml` walking up to repo root.
3. Built-in defaults (see `02_DATA_MODEL.md` §4).
CLI flags (`--fail-on`, etc.) override file config.

## 5. Logging & diagnostics
- `--quiet` suppresses everything but errors.
- On parse errors, print the file, sheet, and (if available) line/token context; never a bare stack
  trace.
- `netdiff validate` is the "does my project even parse" smoke test for CI setup.
