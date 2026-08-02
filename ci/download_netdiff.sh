#!/usr/bin/env bash
# Download a packaged netdiff binary from GitHub Releases (T2.1 assets).
set -euo pipefail

REPO="${1:?usage: download_netdiff.sh <owner/repo> <version|latest> <dest-dir>}"
VERSION="${2:?}"
DEST="${3:?}"

uname_s="$(uname -s | tr '[:upper:]' '[:lower:]')"
uname_m="$(uname -m | tr '[:upper:]' '[:lower:]')"
case "$uname_m" in
  x86_64|amd64) arch=x86_64 ;;
  aarch64|arm64) arch=arm64 ;;
  *) echo "unsupported arch: $uname_m" >&2; exit 2 ;;
esac

case "$uname_s" in
  linux)
    asset="netdiff-linux-${arch}.tar.gz"
    fmt=tar
    ;;
  darwin)
    asset="netdiff-macos-${arch}.tar.gz"
    fmt=tar
    ;;
  mingw*|msys*|cygwin*)
    asset="netdiff-windows-x86_64.zip"
    fmt=zip
    ;;
  *)
    # GitHub windows runners report MINGW / MSYS via uname when using bash.
    if [[ "${RUNNER_OS:-}" == "Windows" ]]; then
      asset="netdiff-windows-x86_64.zip"
      fmt=zip
    else
      echo "unsupported OS: $uname_s" >&2
      exit 2
    fi
    ;;
esac

mkdir -p "$DEST"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

if [[ "$VERSION" == "latest" ]]; then
  url="https://github.com/${REPO}/releases/latest/download/${asset}"
else
  url="https://github.com/${REPO}/releases/download/${VERSION}/${asset}"
fi

echo "Downloading ${url}"
curl -fsSL "$url" -o "${tmpdir}/${asset}"

if [[ "$fmt" == "zip" ]]; then
  python -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" \
    "${tmpdir}/${asset}" "$tmpdir/out"
else
  mkdir -p "$tmpdir/out"
  tar -xzf "${tmpdir}/${asset}" -C "$tmpdir/out"
fi

bin="$(find "$tmpdir/out" -type f \( -name netdiff -o -name netdiff.exe \) | head -n 1)"
if [[ -z "$bin" ]]; then
  echo "netdiff binary missing from ${asset}" >&2
  exit 1
fi

if [[ "$bin" == *.exe ]]; then
  cp "$bin" "${DEST}/netdiff.exe"
  echo "${DEST}/netdiff.exe"
else
  install -m 0755 "$bin" "${DEST}/netdiff"
  echo "${DEST}/netdiff"
fi
