#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROMPT_FILE="${1:-$ROOT_DIR/docs/START_PROMPT.md}"
LOG_DIR="$ROOT_DIR/.codex-run-logs"

mkdir -p "$LOG_DIR"

cd "$ROOT_DIR"

codex exec --full-auto --json "$(cat "$PROMPT_FILE")" \
  | tee "$LOG_DIR/amarantin-minimality-$(date +%Y%m%d-%H%M%S).jsonl"
