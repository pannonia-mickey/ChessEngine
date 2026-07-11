#!/usr/bin/env bash
# Run an SPRT match between a baseline and a candidate build of the engine, to
# validate whether a change is a real strength improvement.
#
#   run_sprt.sh <base_ref> [candidate_ref]
#     base_ref       git ref for the baseline (e.g. HEAD~1, main, a tag)
#     candidate_ref  git ref for the candidate (default: current working tree)
#
# Tunable via environment (defaults shown):
#   SPRT_TC=10+0.1  SPRT_ELO0=0  SPRT_ELO1=5  SPRT_ALPHA=0.05  SPRT_BETA=0.05
#   SPRT_ROUNDS=4000  SPRT_CONCURRENCY=<half of logical cores>  SPRT_TIMEMARGIN=1000
# Cross-platform: Git Bash on Windows (MSVC multi-config) and bash on Linux/Pi.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SPRT_DIR="$ROOT/tools/sprt"
BIN_DIR="$SPRT_DIR/bin"
BOOK="$SPRT_DIR/books/8moves_v3.pgn"
RESULTS_DIR="$SPRT_DIR/results"

# Logical core count: nproc on Linux/Pi, NUMBER_OF_PROCESSORS on Windows Git Bash.
detect_cores() {
  if command -v nproc >/dev/null 2>&1; then nproc
  elif [ -n "${NUMBER_OF_PROCESSORS:-}" ]; then echo "$NUMBER_OF_PROCESSORS"
  else echo 2
  fi
}
half_cores=$(( $(detect_cores) / 2 ))
[ "$half_cores" -ge 1 ] || half_cores=1

SPRT_TC="${SPRT_TC:-10+0.1}"
SPRT_ELO0="${SPRT_ELO0:-0}"
SPRT_ELO1="${SPRT_ELO1:-5}"
SPRT_ALPHA="${SPRT_ALPHA:-0.05}"
SPRT_BETA="${SPRT_BETA:-0.05}"
SPRT_ROUNDS="${SPRT_ROUNDS:-4000}"
SPRT_CONCURRENCY="${SPRT_CONCURRENCY:-$half_cores}"
SPRT_TIMEMARGIN="${SPRT_TIMEMARGIN:-1000}"

usage() { echo "Usage: $0 <base_ref> [candidate_ref]" >&2; exit 2; }
[ $# -ge 1 ] || usage
BASE_REF="$1"
CAND_REF="${2:-}"

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) OS_WIN=1; EXE=".exe" ;;
  *)                    OS_WIN=0; EXE="" ;;
esac

# Convert a POSIX path to a native Windows path for the native fastchess.exe.
winpath() {
  if [ "$OS_WIN" = 1 ] && command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else echo "$1"; fi
}

# Locate fastchess (PATH, or the vendored copy from setup.sh).
FASTCHESS=""
if command -v fastchess >/dev/null 2>&1; then
  FASTCHESS="$(command -v fastchess)"
else
  for c in "$SPRT_DIR/fastchess/fastchess" "$SPRT_DIR/fastchess/fastchess.exe"; do
    [ -x "$c" ] && { FASTCHESS="$c"; break; }
  done
fi
[ -n "$FASTCHESS" ] || { echo "fastchess not found; run tools/sprt/setup.sh" >&2; exit 1; }
[ -f "$BOOK" ]      || { echo "book missing ($BOOK); run tools/sprt/setup.sh" >&2; exit 1; }

mkdir -p "$BIN_DIR" "$RESULTS_DIR"

# Find the engine binary under a build dir across single-/multi-config generators.
find_engine() {
  local b="$1" p
  for p in "$b/chess_engine" "$b/chess_engine.exe" "$b/Release/chess_engine.exe" "$b/Release/chess_engine"; do
    [ -x "$p" ] && { echo "$p"; return 0; }
  done
  return 1
}

# Configure + build the engine (CMAKE_BUILD_TYPE serves single-config, --config
# serves multi-config; the unused one is ignored harmlessly).
build_engine() {
  local src="$1" build="$2"
  cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCHESS_BUILD_TESTS=OFF >/dev/null
  cmake --build "$build" --config Release --target chess_engine >/dev/null
}

# Build a git ref in an isolated worktree, copy its engine to $2. Leaves the
# main working tree untouched.
build_ref() {
  local ref="$1" out="$2" wt
  wt="$(mktemp -d)"
  git -C "$ROOT" worktree add --detach "$wt" "$ref" >/dev/null
  build_engine "$wt" "$wt/build"
  cp "$(find_engine "$wt/build")" "$out"
  git -C "$ROOT" worktree remove --force "$wt"
}

echo ">> Building baseline ($BASE_REF)"
build_ref "$BASE_REF" "$BIN_DIR/base$EXE"

if [ -n "$CAND_REF" ]; then
  echo ">> Building candidate ($CAND_REF)"
  build_ref "$CAND_REF" "$BIN_DIR/candidate$EXE"
else
  echo ">> Building candidate (current working tree)"
  # Build into a dedicated dir so the user's main build/ is left untouched.
  build_engine "$ROOT" "$BIN_DIR/wt-build"
  cp "$(find_engine "$BIN_DIR/wt-build")" "$BIN_DIR/candidate$EXE"
fi

PGN="$RESULTS_DIR/sprt_$(date +%Y%m%d_%H%M%S).pgn"
echo ">> SPRT elo0=$SPRT_ELO0 elo1=$SPRT_ELO1 alpha=$SPRT_ALPHA beta=$SPRT_BETA tc=$SPRT_TC"
# candidate is engine 1, so a positive Elo means the candidate is stronger.
# Run from $RESULTS_DIR so fastchess's own resume-state file (config.json,
# written to the CWD) lands there instead of the repo root.
( cd "$RESULTS_DIR" && "$FASTCHESS" \
  -engine cmd="$(winpath "$BIN_DIR/candidate$EXE")" name=candidate \
  -engine cmd="$(winpath "$BIN_DIR/base$EXE")" name=base \
  -each tc="$SPRT_TC" timemargin="$SPRT_TIMEMARGIN" proto=uci \
  -openings file="$(winpath "$BOOK")" format=pgn order=random \
  -rounds "$SPRT_ROUNDS" -repeat \
  -concurrency "$SPRT_CONCURRENCY" \
  -sprt elo0="$SPRT_ELO0" elo1="$SPRT_ELO1" alpha="$SPRT_ALPHA" beta="$SPRT_BETA" model=normalized \
  -pgnout file="$(winpath "$PGN")" )

echo ">> Games saved to $PGN"
echo ">> 'H1 accepted' => candidate is stronger (keep the change)."
echo ">> 'H0 accepted' => no gain / regression (revert the change)."
