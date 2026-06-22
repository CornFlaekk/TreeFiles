#!/bin/bash
# capture_screenshot.sh - Capture a tmux pane content to a file
# Usage: ./scripts/capture_screenshot.sh <session_name> <output_file>

if [ $# -lt 2 ]; then
    echo "Usage: $0 <session_name> <output_file>"
    echo "Example: $0 treefiles_test screenshots/01_initial.txt"
    exit 1
fi

SESSION="$1"
OUTPUT="$2"

mkdir -p "$(dirname "$OUTPUT")"
tmux capture-pane -t "$SESSION" -p > "$OUTPUT"
echo "Captured pane from '$SESSION' to '$OUTPUT'"
