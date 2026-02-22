#!/bin/bash
# Setup script for git hooks

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$REPO_ROOT"

echo "Configuring git hooks..."
git config core.hooksPath .githooks

echo "✅ Git hooks configured successfully!"
echo "   Pre-commit hook will now run 'bazel test //...' before each commit."
