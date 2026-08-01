#!/usr/bin/env bash
# T0.2 AC: no platform-locked APIs on the engine core path.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ENGINE="$ROOT/engine"
fail=0

check() {
  local pattern="$1"
  local label="$2"
  if grep -RIn --include='*.cpp' --include='*.h' --include='*.hpp' -E "$pattern" "$ENGINE"; then
    echo "FAIL: found $label under engine/"
    fail=1
  else
    echo "OK: no $label under engine/"
  fi
}

check '#include[[:space:]]*<windows\.h>' '<windows.h>'
check 'ShellExecute' 'ShellExecute'
check 'system[[:space:]]*\(' 'system('

exit "$fail"
