#!/usr/bin/env bash
# End-to-end test: pipe a scripted session into the real binary and compare
# stdout byte-for-byte with the expected output.
#
# Usage: run_e2e.sh <path-to-minikv-binary> [directory-with-session-files]
set -euo pipefail

binary="${1:?usage: run_e2e.sh <minikv-binary> [e2e-dir]}"
dir="${2:-$(cd "$(dirname "$0")" && pwd)}"

actual="$(mktemp)"
trap 'rm -f "$actual"' EXIT

# --no-save keeps the test hermetic: nothing is read from or written to disk.
"$binary" --no-save < "$dir/session1.txt" > "$actual"

if diff -u "$dir/session1.expected" "$actual"; then
    echo "e2e_session1: PASS"
else
    echo "e2e_session1: FAIL (diff above: - expected, + actual)" >&2
    exit 1
fi
