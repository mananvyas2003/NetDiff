# NetDiff GitHub Action

Composite action that runs `netdiff diff --git <base> <head>` on pull requests
(`docs/04_CLI_CI_SPEC.md` §2.1).

## Behavior

1. Resolves a `netdiff` binary (input path, or download from GitHub Releases).
2. Diffs `pull_request.base.sha` → `pull_request.head.sha`.
3. Writes markdown + SARIF under the runner temp dir.
4. Optionally posts/updates **one** sticky PR comment (`header: netdiff`).
5. Optionally uploads SARIF via `github/codeql-action/upload-sarif`.
6. Fails the job when the CLI gate fails (exit `1`), unless the project config /
   `--fail-on never` makes the CLI exit `0`.

## Consumer workflow

```yaml
name: netdiff
on:
  pull_request:

permissions:
  contents: read
  pull-requests: write
  security-events: write   # required for SARIF upload

jobs:
  connectivity:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0   # required for --git across commits

      - uses: OWNER/REPO/ci@v0.1.0   # or @master while cutting releases
        with:
          path: .
          # fail-on: significant
          comment: true
          sarif: true
```

### Inputs

| Input | Default | Meaning |
|-------|---------|---------|
| `path` | `.` | Project dir or entry `.kicad_sch` |
| `config` | _(empty)_ | Optional `.netdiff.yml` |
| `fail-on` | _(empty)_ | `significant` / `any` / `never` |
| `comment` | `true` | Sticky markdown PR comment |
| `sarif` | `true` | Upload SARIF |
| `netdiff-binary` | _(empty)_ | Use a pre-built binary instead of downloading |
| `version` | `latest` | Release tag for download (`v0.1.0`, …) |
| `repository` | action repo | `owner/name` that publishes T2.1 assets |
| `token` | `github.token` | Comment / download auth |

### Dogfooding this repository

Build the CLI in CI, then point the action at it (no release required):

```yaml
- uses: actions/checkout@v4
  with:
    fetch-depth: 0
- run: |
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release --parallel --target netdiff_cli
- uses: ./ci
  with:
    netdiff-binary: ${{ runner.os == 'Windows' && 'build/cli/Release/netdiff.exe' || 'build/cli/netdiff' }}
    comment: false
    sarif: false
```

## Local check

```bash
NETDIFF_BIN=build/cli/netdiff \
BASE_SHA=<old> HEAD_SHA=<new> PROJECT_PATH=. OUT_DIR=/tmp/nd \
  python3 ci/run_diff.py
cat /tmp/nd/report.md
```

Or: `python tests/ci/test_action_run_diff.py build/cli/netdiff`

## GitLab CI (`gitlab-netdiff.yml`)

Include the template and set `NETDIFF_REPO` (GitHub owner/name that hosts T2.1 release assets).
Optional: `NETDIFF_VERSION`, `NETDIFF_PATH`, `NETDIFF_FAIL_ON`, `GITLAB_NETDIFF_TOKEN` for MR notes.

## pre-commit (`.pre-commit-hooks.yaml`)

```yaml
repos:
  - repo: https://github.com/OWNER/REPO
    rev: v0.1.0
    hooks:
      - id: netdiff
```

Requires `netdiff` on `PATH` or `NETDIFF_BIN`. Local check:
`python tests/ci/test_pre_commit_hook.py build/cli/netdiff`

