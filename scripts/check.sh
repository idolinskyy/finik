#!/usr/bin/env bash
# Lightweight project checks (no Node / pre-commit framework).
# Usage:
#   ./scripts/check.sh              # format-check on src/ + tests/
#   ./scripts/check.sh format       # same
#   ./scripts/check.sh format-fix  # rewrite in place (use carefully)
#   ./scripts/check.sh test         # run fin_tests if built
#   ./scripts/check.sh all          # format-check + test
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

die() { echo "error: $*" >&2; exit 1; }

need() { command -v "$1" >/dev/null 2>&1 || die "'$1' not found"; }

list_cxx() {
  find src tests -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
    ! -path '*/build/*' | sort
}

cmd_format_check() {
  need clang-format
  local bad=0
  while IFS= read -r f; do
    if ! clang-format --dry-run --Werror "$f" >/dev/null 2>&1; then
      echo "needs format: $f"
      bad=1
    fi
  done < <(list_cxx)
  if [[ "$bad" -ne 0 ]]; then
    echo "Run: ./scripts/check.sh format-fix" >&2
    exit 1
  fi
  echo "clang-format: ok"
}

cmd_format_fix() {
  need clang-format
  list_cxx | xargs -r clang-format -i
  echo "clang-format: fixed"
}

cmd_test() {
  local bin=""
  for c in build/tests/fin_tests build/Desktop-Release/tests/fin_tests; do
    if [[ -x "$c" ]]; then bin="$c"; break; fi
  done
  [[ -n "$bin" ]] || die "fin_tests not found — build first (cmake && make)"
  "$bin"
}

cmd="${1:-format}"
case "$cmd" in
  format|format-check) cmd_format_check ;;
  format-fix)         cmd_format_fix ;;
  test)                cmd_test ;;
  all)
    cmd_format_check
    cmd_test
    ;;
  -h|--help|help)
    sed -n '2,12p' "$0"
    ;;
  *)
    die "unknown command: $cmd (try: format | format-fix | test | all)"
    ;;
esac
