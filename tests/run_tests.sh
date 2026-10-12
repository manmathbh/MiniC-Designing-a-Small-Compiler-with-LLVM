#!/usr/bin/env bash
# tests/run_tests.sh — MiniC test runner
#
# Usage:
#   cd build && ../tests/run_tests.sh          # run all tests
#   cd build && ../tests/run_tests.sh hello    # run only hello.mc
#
# For each <name>.mc:
#   1. Compile with ./minicc
#   2. Run the resulting executable
#   3. Diff output against tests/expected/<name>.txt
#
# Exit code: 0 if all tests pass, 1 otherwise.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$(pwd)"
MINICC="${BUILD_DIR}/minicc"
TESTS_DIR="${SCRIPT_DIR}"
EXPECTED_DIR="${TESTS_DIR}/expected"

if [[ ! -x "${MINICC}" ]]; then
    echo "ERROR: minicc not found at ${MINICC}. Did you build first?"
    exit 1
fi

PASS=0
FAIL=0

# Filter: if arg given, only run that test name (no .mc extension).
FILTER="${1:-}"

for mc_file in "${TESTS_DIR}"/*.mc; do
    name="$(basename "${mc_file}" .mc)"
    [[ -n "${FILTER}" && "${name}" != "${FILTER}" ]] && continue

    expected_file="${EXPECTED_DIR}/${name}.txt"
    if [[ ! -f "${expected_file}" ]]; then
        echo "SKIP  ${name}  (no expected output file)"
        continue
    fi

    # Compile
    tmp_bin="$(mktemp /tmp/minic_test_XXXXXX)"
    if ! "${MINICC}" "${mc_file}" -o "${tmp_bin}" 2>/tmp/minic_err; then
        echo "FAIL  ${name}  (compile error)"
        cat /tmp/minic_err
        FAIL=$((FAIL + 1))
        continue
    fi

    # Run and capture output
    actual="$("${tmp_bin}" 2>&1 || true)"
    expected="$(cat "${expected_file}")"

    if [[ "${actual}" == "${expected}" ]]; then
        echo "PASS  ${name}"
        PASS=$((PASS + 1))
    else
        echo "FAIL  ${name}"
        diff <(echo "${expected}") <(echo "${actual}") | head -20
        FAIL=$((FAIL + 1))
    fi

    rm -f "${tmp_bin}"
done

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed."
[[ "${FAIL}" -eq 0 ]]
