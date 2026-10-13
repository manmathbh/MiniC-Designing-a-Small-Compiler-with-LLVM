#!/usr/bin/env bash
# tests/run_pass_test.sh — Test the StrengthReduction pass with diff-based checks.
#
# Usage (from the build directory):
#   ../tests/run_pass_test.sh
#
# Requires:
#   opt     — LLVM optimiser binary (e.g. opt-17 or opt-18)
#   diff    — standard diff

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$(pwd)"
PASS_SO="${BUILD_DIR}/StrengthReduction.so"
INPUT_LL="${SCRIPT_DIR}/ir/strength_input.ll"
EXPECTED_LL="${SCRIPT_DIR}/ir/strength_expected.ll"
OUTPUT_LL="${BUILD_DIR}/strength_output.ll"

# Find opt binary
OPT="$(command -v opt 2>/dev/null || command -v opt-17 2>/dev/null || command -v opt-18 2>/dev/null || echo "")"
if [[ -z "${OPT}" ]]; then
    echo "ERROR: 'opt' not found on PATH. Install LLVM (apt install llvm-17 or llvm-18)."
    exit 1
fi

if [[ ! -f "${PASS_SO}" ]]; then
    echo "ERROR: StrengthReduction.so not found at ${PASS_SO}. Did you build?"
    exit 1
fi

echo "Using opt: ${OPT}"
echo "Using pass: ${PASS_SO}"

# Run the pass
"${OPT}" \
    -load-pass-plugin="${PASS_SO}" \
    -passes="strength-reduction" \
    -S \
    "${INPUT_LL}" \
    -o "${OUTPUT_LL}"

echo "Pass output → ${OUTPUT_LL}"

# Normalise and compare (strip comments, collapse whitespace for stability)
normalise() {
    grep -v '^;' "$1" | sed 's/[ \t]*$//' | grep -v '^$'
}

if diff <(normalise "${EXPECTED_LL}") <(normalise "${OUTPUT_LL}"); then
    echo "PASS: StrengthReduction output matches expected IR."
    exit 0
else
    echo "FAIL: Output differs from expected IR (shown above)."
    exit 1
fi
