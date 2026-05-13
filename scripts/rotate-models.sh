#!/bin/bash
# rotate-models.sh — Update AGENTS.md § 14 with new model IDs
# Usage: ./scripts/rotate-models.sh
#
# This script helps maintain the weekly model rotation documented in AGENTS.md § 14.
# It reads the current rotation, compares against leaderboards, and prompts for updates.
#
# Requirements:
#   - jq (for JSON parsing)
#   - HF_TOKEN environment variable (for HuggingFace API access)
#
# See AGENTS.md § 14 for model rotation guidelines.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

AGENTS_FILE="AGENTS.md"
SECTION_14_PATTERN="## 14. Weekly model rotation"

echo "=== VMAVIFICIENT Model Rotation Helper ==="
echo "AGENTS.md: $AGENTS_FILE"
echo ""

# ── Verify prerequisites ───────────────────────────────────────────────────────
if ! command -v jq >/dev/null 2>&1; then
	echo "ERROR: jq not found"
	echo "Install: brew install jq  # macOS"
	echo "         apt install jq   # Ubuntu/Debian"
	exit 1
fi

if [ -z "${HF_TOKEN:-}" ]; then
	echo "ERROR: HF_TOKEN not set"
	echo "Create a fine-grained token at: https://huggingface.co/settings/tokens"
	echo "Required scopes: 'Make calls to Inference Providers'"
	exit 1
fi

echo "Prerequisites: OK"
echo ""

# ── Extract current rotation from AGENTS.md ────────────────────────────────────
echo "Extracting current model rotation from AGENTS.md..."

# Find the section
if ! grep -q "$SECTION_14_PATTERN" "$AGENTS_FILE"; then
	echo "ERROR: Could not find '$SECTION_14_PATTERN' in $AGENTS_FILE"
	exit 1
fi

# Extract YAML block (between section header and next section or EOF)
CURRENT_ROTATION=$(sed -n "/$SECTION_14_PATTERN/,/^## /p" "$AGENTS_FILE" |
	grep -E "^(classifier|tier[123]):" -A 20 | head -60)

echo "Current rotation (from AGENTS.md § 14):"
echo "$CURRENT_ROTATION"
echo ""

# ── Check model availability on HuggingFace ────────────────────────────────────
check_availability() {
	local model_id="$1"
	local model_name=$(echo "$model_id" | sed 's|/|-|g')

	echo "Checking $model_id availability..."

	# Query HuggingFace API for inference providers
	available_providers=$(curl -s "https://huggingface.co/api/models/$model_id?expand[]=inferenceProviderMapping" \
		-H "Authorization: Bearer $HF_TOKEN" |
		jq -r '.inferenceProviderMapping | keys[]' 2>/dev/null | head -10 || true)

	if [ -z "$available_providers" ]; then
		echo "  ⚠ No inference providers found (may be offline or unavailable)"
		return 1
	fi

	echo "  ✓ Providers: $(echo "$available_providers" | tr '\n' ' ')"
	return 0
}

echo "Checking model availability..."
echo ""

# Check current models (if they appear in rotation)
if echo "$CURRENT_ROTATION" | grep -q "classifier:"; then
	check_availability "Qwen/Qwen2.5-Coder-3B-Instruct" || true
fi
if echo "$CURRENT_ROTATION" | grep -q "tier1:"; then
	check_availability "Qwen/Qwen2.5-Coder-1.5B-Instruct" || true
fi
if echo "$CURRENT_ROTATION" | grep -q "tier2:"; then
	check_availability "Qwen/Qwen3-32B" || true
fi
if echo "$CURRENT_ROTATION" | grep -q "tier3:"; then
	check_availability "Qwen/Qwen3-Coder-Next" || true
fi

echo ""

# ── Check leaderboards for newer models ────────────────────────────────────────
echo "Checking leaderboards for newer models..."
echo ""

# BigCode MultiPL-E leaderboard (C and Rust performance)
echo "BigCode MultiPL-E (C/Rust performance):"
echo "  https://huggingface.co/spaces/bigcode/bigcode-models-leaderboard"
echo ""
echo "Look for models with:"
echo "  - High MultiPL-E C score"
echo "  - High MultiPL-E Rust score"
echo "  - Low code generation latency"
echo ""

# Open LLM Leaderboard
echo "Open LLM Leaderboard:"
echo "  https://huggingface.co/spaces/open-llm-leaderboard/open_llm_leaderboard"
echo ""
echo "Look for models with:"
echo "  - High HumanEval score (code generation)"
echo "  - High MBPP score (benchmark programming)"
echo "  - Good code quality metrics"
echo ""

# ── Prompt for rotation ────────────────────────────────────────────────────────
echo "=== Model Rotation Decision ==="
echo ""
echo "If you want to update the model rotation, answer these questions:"
echo ""

# Check if user wants to proceed
read -r -p "Proceed with model rotation Chat? [y/N] " response
if [[ ! "$response" =~ ^[Yy]$ ]]; then
	echo "Aborted. Run this script again when you're ready to rotate."
	exit 0
fi

echo ""
echo "💡 Model rotation should happen weekly."
echo "   Update § 14 of AGENTS.md and sync:"
echo "     - ~/.pi/agent/models.json"
echo "     - ~/.pi/agent/model-router.json"
echo ""

# Note: Full model rotation is a manual process that requires:
# 1. Reviewing leaderboards for newer models
# 2. Testing candidate models on real tasks
# 3. Updating AGENTS.md § 14
# 4. Updating companion files (models.json, model-router.json)
# 5. Committing changes: "chore(agents): rotate tier models W<week>"
#
# This script provides the information needed to make the decision.
# The actual rotation is done manually to ensure proper testing.

echo "=== Next Steps ==="
echo ""
echo "1. Review the leaderboards above for candidate models"
echo "2. If upgrading, test the new model:"
echo "   - Tier 1: Test FIM completion in a C function"
echo "   - Tier 2: Multi-file debug task"
echo "   - Tier 3: Planning task (produce task sequence)"
echo ""
echo "3. Update AGENTS.md § 14 with new model IDs"
echo "4. Update companion files:"
echo "   - ~/.pi/agent/models.json"
echo "   - ~/.pi/agent/model-router.json"
echo ""
echo "5. Commit:"
echo '   git add AGENTS.md models.json model-router.json'
echo '   git commit -m "chore(agents): rotate tier models W'$(date +%V)'"'
echo ""
echo "See AGENTS.md § 14 for full rotation requirements."
echo ""
