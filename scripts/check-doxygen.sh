#!/bin/bash
# check-doxygen.sh — Doxygen check per AGENTS.md § 8.6
# Usage: check-doxygen.sh (run from project root)
# Checks for Doxygen warnings on header files

set -euo pipefail

# Check if Doxyfile exists
if [ ! -f "Doxyfile" ]; then
	echo "ERROR: Doxyfile not found"
	exit 1
fi

# Run Doxygen and capture output
DOXYGEN_OUTPUT=$(doxygen Doxyfile 2>&1) || {
	echo "Doxygen: FAILED"
	echo "$DOXYGEN_OUTPUT"
	exit 1
}

# Check for warnings
if echo "$DOXYGEN_OUTPUT" | grep -q "warning:"; then
	echo "Doxygen: FAIL (warnings found)"
	echo "$DOXYGEN_OUTPUT" | grep "warning:"
	exit 1
fi

echo "Doxygen: OK (zero warnings)"
exit 0
