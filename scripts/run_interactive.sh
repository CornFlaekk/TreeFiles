#!/bin/bash
# run_interactive.sh - Run TreeFiles in a tmux session and capture screenshots
# Usage: ./scripts/run_interactive.sh [path_to_scan]

set -euo pipefail

BINARY="./treefiles"
SCAN_PATH="${1:-.}"
SESSION="treefiles_test_$$"
SCREENSHOT_DIR="screenshots"
TERMINAL_WIDTH=100
TERMINAL_HEIGHT=30

mkdir -p "$SCREENSHOT_DIR"

capture() {
    local name="$1"
    local outfile="$SCREENSHOT_DIR/${name}.txt"
    tmux capture-pane -t "$SESSION" -p > "$outfile"
    echo "  -> $outfile"
}

echo "Starting tmux session '$SESSION'..."
tmux new-session -d -s "$SESSION" -x "$TERMINAL_WIDTH" -y "$TERMINAL_HEIGHT" \
    "$BINARY" "$SCAN_PATH"

# Wait for app to render
sleep 0.5

echo "Capturing screenshots..."
capture "01_initial"

# Navigate down twice
tmux send-keys -t "$SESSION" Down
sleep 0.2
tmux send-keys -t "$SESSION" Down
sleep 0.2
capture "02_navigated"

# Expand current directory
tmux send-keys -t "$SESSION" e
sleep 0.3
capture "03_expanded"

# Navigate into children
tmux send-keys -t "$SESSION" Down
sleep 0.1
tmux send-keys -t "$SESSION" Down
sleep 0.1
capture "04_children"

# Open help
tmux send-keys -t "$SESSION" C-h
sleep 0.2
capture "05_help_hidden"

# Show help again
tmux send-keys -t "$SESSION" C-h
sleep 0.2
capture "06_help_shown"

# Quit
tmux send-keys -t "$SESSION" q
sleep 0.3

# Kill session if still alive
tmux kill-session -t "$SESSION" 2>/dev/null || true

echo ""
echo "Done. Screenshots saved in $SCREENSHOT_DIR/:"
ls -la "$SCREENSHOT_DIR"/*.txt 2>/dev/null || echo "  (no screenshots captured)"
