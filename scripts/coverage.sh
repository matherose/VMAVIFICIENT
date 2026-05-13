#!/bin/bash
# coverage.sh — Coverage build and report per AGENTS.md § 8.5 Step 7
# Usage: ./scripts/coverage.sh [binary_name]
#   binary_name: Optional. Default: vmavificient

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

BINARY_NAME="${1:-vmavificient}"

echo "=== VMAVIFICIENT Coverage Report ==="
echo "Binary: $BINARY_NAME"
echo "Project root: $PROJECT_ROOT"
echo ""

# ── Step 1: Configure coverage build ──────────────────────────────────────────
echo "[1/5] Configuring coverage build..."

if ! cmake --preset coverage >/dev/null 2>&1; then
	echo "ERROR: CMake configuration failed"
	exit 1
fi

# Clean previous build artifacts
if [ -d "build/coverage" ]; then
	echo "Cleaning previous build..."
	cmake --build build/coverage --target clean 2>/dev/null || true
fi

echo "✓ Coverage build configured"
echo ""

# ── Step 2: Build with instrumentation ────────────────────────────────────────
echo "[2/5] Building with instrumentation..."

BUILD_OUTPUT=$(cmake --build build/coverage 2>&1) || {
	echo "ERROR: Build failed"
	echo "$BUILD_OUTPUT"
	exit 1
}

echo "✓ Build succeeded"
echo ""

# ── Step 3: Run tests with profiling ──────────────────────────────────────────
echo "[3/5] Running tests to collect coverage data..."

export LLVM_PROFILE_FILE="build/coverage/%p.profraw"

# Run the binary to collect coverage (it may exit with non-zero,
# so we capture the exit code but continue)
CMAKE_OUTPUT=$(ctest --preset coverage --output-on-failure 2>&1) || {
	echo "Note: Some tests failed (continuing with coverage report)"
}

echo "✓ Tests completed (coverage data collected)"
echo ""

# ── Step 4: Merge profile data ────────────────────────────────────────────────
echo "[4/5] Merging profile data..."

PROFRATS=$(find build/coverage -name '*.profraw' 2>/dev/null || true)

if [ -z "$PROFRATS" ]; then
	echo "ERROR: No profile data (.profraw) found"
	echo "Did you run tests?"
	exit 1
fi

echo "Found profile files:"
echo "$PROFRATS" | while read -r f; do echo "  $f"; done

MERGED_PROFDATA="build/coverage/merged.profdata"

llvm-profdata merge -sparse $PROFRATS -o "$MERGED_PROFDATA" || {
	echo "ERROR: llvm-profdata merge failed"
	exit 1
}

echo "✓ Profile data merged: $MERGED_PROFDATA"
echo ""

# ── Step 5: Generate reports ──────────────────────────────────────────────────
echo "[5/5] Generating coverage reports..."

# Ensure binary exists
BINARY_PATH="build/coverage/$BINARY_NAME"
if [ ! -f "$BINARY_PATH" ]; then
	# Try alternate paths
	for path in build/coverage/src/$BINARY_NAME build/$BINARY_NAME; do
		if [ -f "$path" ]; then
			BINARY_PATH="$path"
			break
		fi
	done
fi

if [ ! -f "$BINARY_PATH" ]; then
	echo "ERROR: Binary not found at expected paths"
	echo "  build/coverage/$BINARY_NAME"
	echo "  build/coverage/src/$BINARY_NAME"
	echo "  build/$BINARY_NAME"
	exit 1
fi

echo "Binary: $BINARY_PATH"
echo ""

# Directory for reports
REPORT_DIR="reports/coverage"
mkdir -p "$REPORT_DIR"

# Text report to stdout
echo "=== Line coverage summary ==="
llvm-cov report "$BINARY_PATH" \
	-instr-profile="$MERGED_PROFDATA" \
	--show-instantiation-summary \
	--ignore-filename-regex="tests/" 2>&1

echo ""
echo "=== Branch coverage ==="
llvm-cov report "$BINARY_PATH" \
	-instr-profile="$MERGED_PROFDATA" \
	--show-branch-summary \
	--ignore-filename-regex="tests/" 2>&1 | grep -E "TOTAL|Branch" || true

echo ""
echo "=== Function coverage ==="
llvm-cov report "$BINARY_PATH" \
	-instr-profile="$MERGED_PROFDATA" \
	--show-function-summary \
	--ignore-filename-regex="tests/" 2>&1 | grep -E "TOTAL|Function" || true

echo ""
echo "=== HTML report ==="
llvm-cov show "$BINARY_PATH" \
	-instr-profile="$MERGED_PROFDATA" \
	-format=html \
	-output-dir="$REPORT_DIR" \
	--ignore-filename-regex="tests/" 2>&1 || {
	echo "Note: HTML report generation has warnings (check for false positives)"
}

echo ""
echo "HTML report: $REPORT_DIR/index.html"
echo "Open in browser to view interactive coverage visualization."

echo ""
echo "=== Coverage Report Complete ==="
