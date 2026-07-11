#!/usr/bin/env bash
# One-time setup for the SPRT tool: obtain fastchess and download an opening book.
# Cross-platform: builds fastchess from source on Linux/Raspberry Pi, and
# downloads the prebuilt fastchess.exe release in Git Bash on Windows.
set -euo pipefail

SPRT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOKS_DIR="$SPRT_DIR/books"
FC_DIR="$SPRT_DIR/fastchess"
FC_REPO="https://github.com/Disservin/fastchess"
BOOK_URL="https://github.com/official-stockfish/books/raw/master/8moves_v3.pgn.zip"

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) OS_WIN=1 ;;
  *)                    OS_WIN=0 ;;
esac

mkdir -p "$BOOKS_DIR" "$FC_DIR"

# 1. fastchess (Qt-free; prebuilt on Windows, built from source on Linux/ARM).
if command -v fastchess >/dev/null 2>&1; then
  echo ">> fastchess already on PATH: $(command -v fastchess)"
elif [ -x "$FC_DIR/fastchess" ] || [ -x "$FC_DIR/fastchess.exe" ]; then
  echo ">> fastchess already present in $FC_DIR"
elif [ "$OS_WIN" = 1 ]; then
  echo ">> Downloading prebuilt fastchess (latest release, Windows x86-64 zip)"
  url="$(curl -fsSL https://api.github.com/repos/Disservin/fastchess/releases/latest \
        | grep -i 'browser_download_url' \
        | grep -iE 'windows-x86-64\.zip"' \
        | head -1 | cut -d'"' -f4)"
  [ -n "$url" ] || { echo "!! No Windows fastchess asset found; download one into $FC_DIR/fastchess.exe manually" >&2; exit 1; }
  tmpzip="$(mktemp)"
  tmpextract="$(mktemp -d)"
  curl -fSL "$url" -o "$tmpzip"
  if command -v unzip >/dev/null 2>&1; then
    unzip -o "$tmpzip" -d "$tmpextract"
  else
    tar -xf "$tmpzip" -C "$tmpextract"   # bsdtar (Windows 10+/libarchive) extracts zip
  fi
  # The release zip wraps everything in a "fastchess-windows-*" subdirectory;
  # lift fastchess.exe up to where run_sprt.sh expects it.
  find "$tmpextract" -name 'fastchess.exe' -exec mv {} "$FC_DIR/fastchess.exe" \;
  rm -rf "$tmpzip" "$tmpextract"
else
  echo ">> Cloning and building fastchess from source"
  [ -d "$FC_DIR/.git" ] || git clone --depth 1 "$FC_REPO" "$FC_DIR"
  make -C "$FC_DIR" -j
fi

# 2. Opening book (the engine is deterministic, so varied starts are mandatory).
if [ -f "$BOOKS_DIR/8moves_v3.pgn" ]; then
  echo ">> Opening book already present"
else
  echo ">> Downloading opening book"
  tmpzip="$(mktemp)"
  curl -fSL "$BOOK_URL" -o "$tmpzip"
  if command -v unzip >/dev/null 2>&1; then
    unzip -o "$tmpzip" -d "$BOOKS_DIR"
  else
    tar -xf "$tmpzip" -C "$BOOKS_DIR"   # bsdtar (Windows 10+/libarchive) extracts zip
  fi
  rm -f "$tmpzip"
fi

echo ">> Setup complete."
