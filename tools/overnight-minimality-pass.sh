#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROMPT_FILE="${1:-$ROOT_DIR/docs/START_PROMPT.md}"
LOG_DIR="$ROOT_DIR/.codex-run-logs"
TIME_BUDGET_SECS="${AMARANTIN_MINIMALITY_TIME_BUDGET_SECS:-3600}"
INTER_RUN_PAUSE_SECS="${AMARANTIN_MINIMALITY_PAUSE_SECS:-1}"

mkdir -p "$LOG_DIR"

cd "$ROOT_DIR"

if [[ ! -f "$PROMPT_FILE" ]]; then
  printf 'prompt file not found: %s\n' "$PROMPT_FILE" >&2
  exit 1
fi

require_integer() {
  local name=$1
  local value=$2
  local description=$3
  if [[ ! "$value" =~ ^[0-9]+$ ]]; then
    printf '%s must be a %s: %s\n' "$name" "$description" "$value" >&2
    exit 1
  fi
}

require_integer "AMARANTIN_MINIMALITY_TIME_BUDGET_SECS" "$TIME_BUDGET_SECS" "positive integer"

if (( TIME_BUDGET_SECS <= 0 )); then
  printf 'AMARANTIN_MINIMALITY_TIME_BUDGET_SECS must be greater than zero: %s\n' \
    "$TIME_BUDGET_SECS" >&2
  exit 1
fi

require_integer "AMARANTIN_MINIMALITY_PAUSE_SECS" "$INTER_RUN_PAUSE_SECS" "non-negative integer"

SECONDS=0
run_index=1

while (( SECONDS < TIME_BUDGET_SECS )); do
  timestamp="$(date +%Y%m%d-%H%M%S)"
  run_label="$(printf 'run%02d' "$run_index")"
  log_path="$LOG_DIR/amarantin-minimality-${timestamp}-${run_label}.jsonl"
  remaining_secs=$((TIME_BUDGET_SECS - SECONDS))

  printf 'starting Codex pass %d with %ds remaining; log: %s\n' \
    "$run_index" "$remaining_secs" "$log_path" >&2

  codex exec --full-auto --json "$(<"$PROMPT_FILE")" \
    | tee "$log_path"

  run_index=$((run_index + 1))

  if (( INTER_RUN_PAUSE_SECS > 0 && SECONDS < TIME_BUDGET_SECS )); then
    sleep "$INTER_RUN_PAUSE_SECS"
  fi
done

printf 'time budget exhausted after %d Codex pass(es)\n' "$((run_index - 1))" >&2
