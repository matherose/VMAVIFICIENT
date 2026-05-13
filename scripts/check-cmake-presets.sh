#!/bin/bash
# check-cmake-presets.sh — CMakePresets.json validation
# Usage: check-cmake-presets.sh (run from project root)

set -euo pipefail

# Check if CMakePresets.json exists
if [ ! -f "CMakePresets.json" ]; then
	echo "ERROR: CMakePresets.json not found"
	exit 1
fi

# Validate JSON syntax
if ! python3 -c "import json; json.load(open('CMakePresets.json'))" 2>/dev/null; then
	echo "CMakePresets.json: INVALID JSON"
	exit 1
fi

# Check required presets exist
REQUIRED_PRESETS=("debug" "asan" "msan" "tsan" "coverage" "release")
for preset in "${REQUIRED_PRESETS[@]}"; do
	if ! grep -q "\"name\": \"$preset\"" CMakePresets.json; then
		echo "CMakePresets.json: Missing required preset: $preset"
		exit 1
	fi
done

echo "CMakePresets.json: OK (valid JSON, all presets present)"
exit 0
