#!/bin/bash
set -euo pipefail

srcdir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$srcdir"

if [ -f "./.setup.sh" ]; then
  # shellcheck disable=SC1091
  source "./.setup.sh"
fi

for tool in autoreconf autoconf automake aclocal; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "error: missing required tool '$tool'" >&2
    exit 1
  }
done

mkdir -p m4
autoreconf --install --force --verbose

exec ./configure "$@"
