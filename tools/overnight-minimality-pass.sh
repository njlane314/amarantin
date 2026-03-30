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

case "$TIME_BUDGET_SECS" in
  ''|*[!0-9]*)
    printf 'AMARANTIN_MINIMALITY_TIME_BUDGET_SECS must be a positive integer: %s\n' \
      "$TIME_BUDGET_SECS" >&2
    exit 1
    ;;
esac

if (( TIME_BUDGET_SECS <= 0 )); then
  printf 'AMARANTIN_MINIMALITY_TIME_BUDGET_SECS must be greater than zero: %s\n' \
    "$TIME_BUDGET_SECS" >&2
  exit 1
fi

case "$INTER_RUN_PAUSE_SECS" in
  ''|*[!0-9]*)
    printf 'AMARANTIN_MINIMALITY_PAUSE_SECS must be a non-negative integer: %s\n' \
      "$INTER_RUN_PAUSE_SECS" >&2
    exit 1
    ;;
esac

start_epoch="$(date +%s)"
deadline_epoch=$((start_epoch + TIME_BUDGET_SECS))
run_index=1

while :; do
  now_epoch="$(date +%s)"
  if (( now_epoch >= deadline_epoch )); then
    break
  fi

  timestamp="$(date +%Y%m%d-%H%M%S)"
  run_label="$(printf 'run%02d' "$run_index")"
  log_path="$LOG_DIR/amarantin-minimality-${timestamp}-${run_label}.jsonl"
  remaining_secs=$((deadline_epoch - now_epoch))

  printf 'starting Codex pass %d with %ds remaining; log: %s\n' \
    "$run_index" "$remaining_secs" "$log_path" >&2

  codex exec --full-auto --json "$(cat "$PROMPT_FILE")" \
    | tee "$log_path"

  run_index=$((run_index + 1))

  if (( INTER_RUN_PAUSE_SECS > 0 )); then
    now_epoch="$(date +%s)"
    if (( now_epoch < deadline_epoch )); then
      sleep "$INTER_RUN_PAUSE_SECS"
    fi
  fi
done

printf 'time budget exhausted after %d Codex pass(es)\n' "$((run_index - 1))" >&2
