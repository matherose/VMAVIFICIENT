#!/bin/bash
# fuzz.sh — Run all fuzz targets per AGENTS.md § 8.5 Step 8
# Usage: ./scripts/fuzz.sh [target_name] [duration_seconds]
#   target_name: Optional. Default: all targets in fuzz/
#   duration_seconds: Optional. Default: 60 seconds

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

TARGET="${1:-}"
DURATION="${2:-60}"

echo "=== VMAVIFICIENT Fuzzing ==="
echo "Project root: $PROJECT_ROOT"
echo "Target: ${TARGET:-all}"
echo "Duration: ${DURATION}s"
echo ""

# ── Check for libFuzzer support ────────────────────────────────────────────────
if ! clang -fsanitize=fuzzer -x c -o /dev/null - <<<'int main() { return 0; }' 2>&1; then
	echo "ERROR: libFuzzer not supported by this Clang"
	echo "Check: clang --version (should be 6.0+)"
	exit 1
fi

echo "Clang supports libFuzzer"
echo ""

# ── Ensure build directory exists ──────────────────────────────────────────────
if [ ! -d "build" ]; then
	echo "Configuring debug build for fuzzing..."
	cmake --preset debug >/dev/null 2>&1
fi

# ── Find fuzz targets ──────────────────────────────────────────────────────────
FUZZ_DIR="fuzz"

if [ ! -d "$FUZZ_DIR" ]; then
	echo "ERROR: fuzz/ directory not found"
	exit 1
fi

FUZZ_TARGETS=$(find "$FUZZ_DIR" -maxdepth 1 -name 'fuzz_*.c' -type f 2>/dev/null || true)

if [ -z "$FUZZ_TARGETS" ]; then
	echo "ERROR: No fuzz targets found in fuzz/"
	echo "Expected: fuzz/fuzz_*.c"
	exit 1
fi

echo "Found fuzz targets:"
echo "$FUZZ_TARGETS" | while read -r f; do
	basename "$f"
done
echo ""

# ── Compile fuzz targets (if not already compiled) ────────────────────────────
COMPILATION_NEEDED=false
if [ -z "$(ls -A "$FUZZ_DIR"/*.fuzz 2>/dev/null)" ]; then
	echo "Compiling fuzz targets..."
	COMPILATION_NEEDED=true
fi

if [ -n "$TARGET" ]; then
	# Check specific target
	if [ ! -f "$FUZZ_DIR/$TARGET.fuzz" ]; then
		echo "Compiling $TARGET.fuzz..."
		COMPILATION_NEEDED=true
	fi
fi

if [ "$COMPILATION_NEEDED" = true ]; then
	# Compile all targets
	for source in $FUZZ_TARGETS; do
		target=$(basename "$source" .c)
		echo "Compiling: $target.fuzz"

		# Extract module name from fuzz_foo.c -> foo
		module=$(echo "$target" | sed 's/^fuzz_//')

		# Find source files for this module
		MODULE_SRC=""
		if [ -f "src/$module/$module.c" ]; then
			MODULE_SRC="src/$module/$module.c"
		else
			# Fallback: search for module in src/
			MODULE_SRC=$(find src -name "$module.c" -type f | head -1)
		fi

		if [ -z "$MODULE_SRC" ]; then
			echo "  WARNING: Could not find module source for $module"
			continue
		fi

		# Find include path
		MODULE_INCLUDE=""
		if [ -f "include/vmavificient/$module.h" ]; then
			MODULE_INCLUDE="-Iinclude/vmavificient"
		elif [ -f "include/$module.h" ]; then
			MODULE_INCLUDE="-Iinclude"
		else
			echo "  WARNING: Could not find module header for $module"
		fi

		# Compile with ASan + libFuzzer
		clang -std=c11 -pedantic-errors \
			-fsanitize=fuzzer,address \
			-Iinclude \
			$MODULE_INCLUDE \
			$MODULE_SRC \
			"$source" \
			-o "$FUZZ_DIR/$target.fuzz" 2>&1 || {
			echo "  ERROR: Compilation failed"
			continue
		}

		echo "  ✓ Compiled: $FUZZ_DIR/$target.fuzz"
	done
	echo ""
fi

# ── Run fuzz targets ───────────────────────────────────────────────────────────
echo "Running fuzz targets for ${DURATION}s each..."
echo ""

# Create findings directory
mkdir -p fuzz/findings

# Find compiled fuzzers
FUZZERS=$(find "$FUZZ_DIR" -maxdepth 1 -name '*.fuzz' -type f 2>/dev/null || true)

if [ -z "$FUZZERS" ]; then
	echo "ERROR: No compiled fuzzers found"
	exit 1
fi

for fuzzer in $FUZZERS; do
	target_name=$(basename "$fuzzer" .fuzz)
	module=$(echo "$target_name" | sed 's/^fuzz_//')

	echo "=== Fuzzing: $target_name ==="

	# Create corpus directory if it doesn't exist
	corpus_dir="fuzz/corpus/$module"
	if [ ! -d "$corpus_dir" ]; then
		mkdir -p "$corpus_dir"
		echo "  Created corpus directory: $corpus_dir"
	fi

	# Check for existing corpus seeds
	if [ -n "$(ls -A "$corpus_dir" 2>/dev/null)" ]; then
		echo "  Corpus seeds: $(ls "$corpus_dir" | wc -l) files"
	else
		echo "  Corpus: empty (will use libFuzzer's default seeded corpus)"
	fi

	# Run fuzzer
	echo "  Running for ${DURATION}s..."
	echo "  Findings will be saved to: fuzz/findings/$target_name/"

	if [ -n "$(ls -A "$corpus_dir" 2>/dev/null)" ]; then
		# Use existing corpus
		"$fuzzer" \
			-max_total_time="$DURATION" \
			-artifact_prefix="fuzz/findings/$target_name/" \
			"$corpus_dir" 2>&1 || {
			echo "  Note: Fuzzer terminated (this is normal for timeout)"
		}
	else
		# No corpus seeds, let libFuzzer generate its own
		"$fuzzer" \
			-max_total_time="$DURATION" \
			-artifact_prefix="fuzz/findings/$target_name/" \
			fuzz/corpus/_auto_ 2>&1 || {
			echo "  Note: Fuzzer terminated (this is normal for timeout)"
		}
	fi

	# Check for crashes/fuzz findings
	CRASHES=$(ls -1 "fuzz/findings/$target_name/" 2>/dev/null | wc -l || echo "0")
	if [ "$CRASHES" -gt 0 ]; then
		echo "  ⚠ Found $CRASHES potential bugs/crashes!"
		echo "  Review: fuzz/findings/$target_name/"
	else
		echo "  ✓ No crashes found (target is robust for ${DURATION}s)"
	fi
	echo ""
done

# ── Summary ────────────────────────────────────────────────────────────────────
echo "=== Fuzzing Complete ==="
echo ""
echo "Findings location: fuzz/findings/"
echo ""
echo "If crashes were found:"
echo "  1. Review minimized test cases in fuzz/findings/<target>/"
echo "  2. Reproduce locally: ./fuzz/<target>.fuzz fuzz/findings/<target>/*"
echo "  3. Add crash cases to fuzz/corpus/<module>/ for regression testing"
echo ""
echo "To run a specific target:"
echo "  ./scripts/fuzz.sh <target> <duration>"
echo "  Example: ./scripts/fuzz.sh fuzz_config 120"
echo ""
