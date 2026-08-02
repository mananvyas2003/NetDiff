#!/usr/bin/env bash
# T2.1 — install the latest NetDiff release binary (no toolchain required).
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/<owner>/<repo>/master/scripts/install.sh | bash
#   NETDIFF_REPO=owner/repo bash scripts/install.sh
#   bash scripts/install.sh --version v0.1.0 --dir ~/.local/bin
#
# Env:
#   NETDIFF_REPO   GitHub owner/repo (required unless --repo is set)
#   NETDIFF_DIR    install directory (default: /usr/local/bin if writable, else ~/.local/bin)

set -euo pipefail

REPO="${NETDIFF_REPO:-}"
VERSION="latest"
DIR="${NETDIFF_DIR:-}"
PREFIX="https://github.com"

usage() {
  cat <<'EOF'
Install NetDiff from GitHub Releases.

Options:
  --repo owner/name   GitHub repository (or set NETDIFF_REPO)
  --version vX.Y.Z    Release tag (default: latest)
  --dir PATH          Install directory
  -h, --help          Show help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo) REPO="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --dir) DIR="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z "$REPO" ]]; then
  echo "error: set NETDIFF_REPO=owner/repo or pass --repo owner/repo" >&2
  exit 2
fi

uname_s="$(uname -s | tr '[:upper:]' '[:lower:]')"
uname_m="$(uname -m | tr '[:upper:]' '[:lower:]')"
case "$uname_m" in
  x86_64|amd64) arch=x86_64 ;;
  aarch64|arm64) arch=arm64 ;;
  *) echo "error: unsupported arch: $uname_m" >&2; exit 2 ;;
esac

case "$uname_s" in
  linux)  asset="netdiff-linux-${arch}.tar.gz" ;;
  darwin) asset="netdiff-macos-${arch}.tar.gz" ;;
  mingw*|msys*|cygwin*)
    echo "error: use the Windows .zip from Releases, or package_binary.py locally" >&2
    exit 2
    ;;
  *) echo "error: unsupported OS: $uname_s" >&2; exit 2 ;;
esac

if [[ -z "$DIR" ]]; then
  if [[ -w /usr/local/bin ]]; then
    DIR=/usr/local/bin
  else
    DIR="${HOME}/.local/bin"
  fi
fi
mkdir -p "$DIR"

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

if [[ "$VERSION" == "latest" ]]; then
  url="${PREFIX}/${REPO}/releases/latest/download/${asset}"
else
  url="${PREFIX}/${REPO}/releases/download/${VERSION}/${asset}"
fi

echo "downloading ${url}"
curl -fsSL "$url" -o "${tmpdir}/${asset}"
tar -xzf "${tmpdir}/${asset}" -C "$tmpdir"

# Archive may unpack flat or under a top-level folder.
bin=""
if [[ -x "${tmpdir}/netdiff" ]]; then
  bin="${tmpdir}/netdiff"
else
  bin="$(find "$tmpdir" -type f -name netdiff | head -n 1)"
fi
if [[ -z "$bin" || ! -x "$bin" ]]; then
  echo "error: netdiff binary missing from archive" >&2
  exit 1
fi

install -m 0755 "$bin" "${DIR}/netdiff"
echo "installed ${DIR}/netdiff"
"${DIR}/netdiff" version
