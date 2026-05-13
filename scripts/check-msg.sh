#!/bin/bash
# check-msg.sh — Commit message format check per AGENTS.md § 12
# Usage: check-msg.sh <path-to-commit-msg-file>

set -euo pipefail

MSG_FILE="$1"

if [ ! -f "$MSG_FILE" ]; then
	echo "ERROR: Commit message file not found: $MSG_FILE"
	exit 1
fi

# Extract subject line (first line)
HEAD=$(head -1 "$MSG_FILE")

# Pattern: <type>(<scope>): <imperative subject, max 72 chars>
# Types: feat, fix, refactor, chore, docs, test, perf
PATTERN='^(feat|fix|refactor|chore|docs|test|perf)\([a-z_]+\): .{1,72}$'

if echo "$HEAD" | grep -qP "$PATTERN"; then
	echo "Commit message format: OK"
	exit 0
else
	echo "ERROR: Commit subject must match: <type>(<scope>): <subject>"
	echo "  Got: $HEAD"
	echo ""
	echo "Rule: Max 72 chars, type in {feat,fix,refactor,chore,docs,test,perf}"
	echo "Example: fix(allocator): free scratch buffer on early return"
	exit 1
fi
