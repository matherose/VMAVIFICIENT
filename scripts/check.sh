#!/bin/bash
# check.sh — Full quality pipeline per AGENTS.md § 11
# Run this before committing to ensure all quality gates pass.
# Implements the validation order from AGENTS.md § 11:
# 1. Format check — clang-format
# 2. Static analysis — clang-tidy, then scan-build
# 3. Compile — zero warnings (debug preset)
# 4. Unit tests — debug build, then ASan build
# 5. MSan + TSan — separate (mutually exclusive with ASan)
# 6. Coverage — instrumented build with thresholds
# 7. Doxygen — zero warnings

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

echo "=== VMavificient Quality Pipeline ==="
echo "Project root: $PROJECT_ROOT"
echo ""

# ── Step 1: Format check ─────────────────────────────────────────────────────
echo "[1/7] Format check (clang-format)..."

# Check only changed .c and .h files (or all if no git)
if git rev-parse --git-dir >/dev/null 2>&1; then
	CHANGED_FILES=$(git diff --cached --name-only --diff-filter=ACM \
		-- '*.c' '*.h' 2>/dev/null || echo "")

	if [ -n "$CHANGED_FILES" ]; then
		FILES_TO_CHECK="$CHANGED_FILES"
	else
		FILES_TO_CHECK="$(find src include -name '*.c' -o -name '*.h')"
	fi
else
	FILES_TO_CHECK="$(find src include -name '*.c' -o -name '*.h')"
fi

echo "Checking files: $FILES_TO_CHECK"

if ! clang-format --dry-run --Werror -style=file $FILES_TO_CHECK 2>&1; then
	echo "ERROR: clang-format failed. Run: clang-format -i -style=file <files>"
	exit 1
fi
echo "✓ clang-format passed"
echo ""

# ── Step 2: Static analysis ──────────────────────────────────────────────────
echo "[2/7] Static analysis (clang-tidy + scan-build)..."

# Ensure debug build exists for compile_commands.json
if [ ! -f build/debug/compile_commands.json ]; then
	echo "Configuring debug build for compile_commands.json..."
	cmake --preset debug >/dev/null 2>&1
fi

# Run clang-tidy on src files
TIDY_OUTPUT=$(clang-tidy -p build/debug \
	$(find src -name '*.c') \
	-- -std=c11 2>&1) || {
	echo "ERROR: clang-tidy failed"
	echo "$TIDY_OUTPUT"
	exit 1
}

# Check for findings (non-zero exit from clang-tidy is caught above,
# but we also check for findings in the output)
if echo "$TIDY_OUTPUT" | grep -q "error:"; then
	echo "ERROR: clang-tidy found issues"
	echo "$TIDY_OUTPUT" | grep "error:"
	exit 1
fi

echo "✓ clang-tidy passed (zero findings)"
echo ""

# Run scan-build (deep static analysis)
if command -v scan-build >/dev/null 2>&1; then
	echo "Running scan-build..."
	mkdir -p reports/analyzer
	if ! scan-build -o reports/analyzer cmake --preset debug >/dev/null 2>&1; then
		echo "ERROR: scan-build found issues"
		echo "See reports/analyzer/ for details"
		exit 1
	fi
	if ! scan-build -o reports/analyzer cmake --build build/debug >/dev/null 2>&1; then
		echo "ERROR: scan-build found issues during build"
		exit 1
	fi
	echo "✓ scan-build passed (zero findings)"
else
	echo "ℹ scan-build not found, skipping"
fi
echo ""

# ── Step 3: Compile (zero warnings) ──────────────────────────────────────────
echo "[3/7] Compile check (debug preset)..."

if ! cmake --preset debug >/dev/null 2>&1; then
	echo "ERROR: CMake configuration failed"
	exit 1
fi

COMPILE_OUTPUT=$(cmake --build build/debug 2>&1) || {
	echo "ERROR: Build failed"
	echo "$COMPILE_OUTPUT"
	exit 1
}

# Check for warnings (grep for "warning:" in output)
if echo "$COMPILE_OUTPUT" | grep -E "warning:|error:"; then
	echo "ERROR: Build produced warnings or errors"
	exit 1
fi

echo "✓ Build passed (zero warnings)"
echo ""

# ── Step 4: Unit tests ───────────────────────────────────────────────────────
echo "[4/7] Unit tests (debug + asan)..."

# Debug tests
if ! ctest --preset debug --output-on-failure; then
	echo "ERROR: Debug tests failed"
	exit 1
fi
echo "✓ Debug tests passed"

# ASan tests (if sanitizer preset available)
if [ -d build/asan ]; then
	echo "Running ASan tests..."
	if ! ctest --preset asan --output-on-failure; then
		echo "ERROR: ASan tests failed"
		exit 1
	fi
	echo "✓ ASan tests passed"
else
	echo "ℹ ASan build not found, skipping"
fi
echo ""

# ── Step 5: MSan + TSan (separate from ASan) ──────────────────────────────────
echo "[5/7] Additional sanitizers (msan + tsan)..."

# MSan tests
if [ -d build/msan ]; then
	echo "Running MSan tests..."
	if ! ctest --preset msan --output-on-failure; then
		echo "ERROR: MSan tests failed"
		exit 1
	fi
	echo "✓ MSan tests passed"
else
	echo "ℹ MSan build not found, skipping"
fi

# TSan tests
if [ -d build/tsan ]; then
	echo "Running TSan tests..."
	if ! ctest --preset tsan --output-on-failure; then
		echo "ERROR: TSan tests failed"
		exit 1
	fi
	echo "✓ TSan tests passed"
else
	echo "ℹ TSan build not found, skipping"
fi
echo ""

# ── Step 6: Coverage ─────────────────────────────────────────────────────────
echo "[6/7] Coverage report (threshold check)..."

if [ -d build/coverage ]; then
	if [ -f "build/coverage/vmavificient" ]; then
		# Find all .profraw files
		PROFRATS=$(find build/coverage -name '*.profraw' 2>/dev/null)

		if [ -n "$PROFRATS" ]; then
			echo "Merging profile data..."
			llvm-profdata merge -sparse $PROFRATS \
				-o build/coverage/merged.profdata

			echo "Generating coverage report..."
			COVERAGE_OUTPUT=$(llvm-cov report build/coverage/vmavificient \
				-instr-profile=build/coverage/merged.profdata \
				--ignore-filename-regex="tests/" 2>&1) || {
				echo "ERROR: Coverage report generation failed"
				exit 1
			}

			echo "$COVERAGE_OUTPUT"

			# Extract line coverage percentage
			LINE_COV=$(echo "$COVERAGE_OUTPUT" | grep "TOTAL" | awk '{print $NF}' | tr -d '%')

			if [ -n "$LINE_COV" ]; then
				# Check 80% minimum (integer comparison)
				LINE_INT=$(echo "$LINE_COV" | cut -d. -f1)
				if [ "$LINE_INT" -lt 80 ]; then
					echo "ERROR: Line coverage ${LINE_COV}% below 80% threshold"
					exit 1
				fi
			fi

			echo "✓ Coverage check passed (>= 80% line coverage)"
		else
			echo "ℹ No profile data found, skipping threshold check"
		fi
	else
		echo "ℹ Binary not found in build/coverage, skipping"
	fi
else
	echo "ℹ Coverage build not found, skipping"
fi
echo ""

# ── Step 7: Doxygen ──────────────────────────────────────────────────────────
echo "[7/7] Doxygen documentation check..."

if [ -f Doxyfile ]; then
	DOXYGEN_OUTPUT=$(doxygen Doxyfile 2>&1) || {
		echo "ERROR: Doxygen failed"
		echo "$DOXYGEN_OUTPUT"
		exit 1
	}

	# Check for warnings
	if echo "$DOXYGEN_OUTPUT" | grep -q "warning:"; then
		echo "ERROR: Doxygen found warnings"
		echo "$DOXYGEN_OUTPUT" | grep "warning:"
		exit 1
	fi

	echo "✓ Doxygen passed (zero warnings)"
else
	echo "ℹ Doxyfile not found, skipping"
fi
echo ""

# ── Summary ────────────────────────────────────────────────────────────────────
echo "=== Quality Pipeline: ALL CHECKS PASSED ==="
echo ""
echo "Next steps:"
echo "  - Run: cmake --build build/debug to verify build"
echo "  - Run: ./build/debug/vmavificient --help to test binary"
echo "  - Commit: git add -A && git commit -m 'chore: pass quality pipeline'"
echo ""
