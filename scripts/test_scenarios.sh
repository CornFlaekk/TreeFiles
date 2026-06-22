#!/bin/bash
set -euo pipefail

BINARY="./treefiles"
HEADLESS="--headless"
TEST_DIR="/tmp/treefiles_test_integration"
PASS=0
FAIL=0

cleanup() { rm -rf "$TEST_DIR"; }

setup() {
    cleanup
    mkdir -p "$TEST_DIR/dir_a" "$TEST_DIR/dir_b" "$TEST_DIR/dir_a/sub"
    dd if=/dev/zero of="$TEST_DIR/dir_a/big.txt"   bs=10240 count=1 2>/dev/null
    dd if=/dev/zero of="$TEST_DIR/dir_b/medium.txt" bs=1024  count=1 2>/dev/null
    dd if=/dev/zero of="$TEST_DIR/dir_a/small.txt"  bs=100   count=1 2>/dev/null
    dd if=/dev/zero of="$TEST_DIR/root_file.txt"    bs=500   count=1 2>/dev/null
    dd if=/dev/zero of="$TEST_DIR/dir_a/sub/deep.txt" bs=512 count=1 2>/dev/null
}

check() {
    local name="$1"; shift
    local output="$1"; shift
    local pattern="$1"; shift
    if echo "$output" | grep -q "$pattern"; then
        echo "  PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $name"
        echo "    pattern: $pattern"
        FAIL=$((FAIL + 1))
    fi
}

check_not() {
    local name="$1"; shift
    local output="$1"; shift
    local pattern="$1"; shift
    if echo "$output" | grep -q "$pattern"; then
        echo "  FAIL: $name (unexpected match)"
        FAIL=$((FAIL + 1))
    else
        echo "  PASS: $name"
        PASS=$((PASS + 1))
    fi
}

extract_frame() {
    local output="$1"
    local num="$2"
    echo "$output" | awk "/=== FRAME $num ===/,/=== END FRAME ===/"
}

run_headless() {
    local events="$1"
    echo "$events" | "$BINARY" "$HEADLESS" "$TEST_DIR" 2>&1
}

echo "Building..."
make clean > /dev/null 2>&1
make > /dev/null 2>&1
echo ""

# ============================================================
echo "=== Scenario 1: Initial state ==="
setup
output=$(run_headless "q")

check "frame 0 exists"       "$output" "=== FRAME 0 ==="
check "selected_index: 0"    "$output" "selected_index: 0"
check "scroll_offset: 0"     "$output" "scroll_offset: 0"
check "show_help: true"      "$output" "show_help: true"
check "expanded_dirs empty"  "$output" "expanded_dirs: {}"
check "dir_a listed"         "$output" '\[DIR\]  dir_a'
check "dir_b listed"         "$output" '\[DIR\]  dir_b'
check "root_file.txt listed" "$output" '\[FILE\] root_file.txt'
check "dir_a first marker"   "$output" '>>> \[DIR\]  dir_a'
check "bar chars present"    "$output" "█"

echo ""

# ============================================================
echo "=== Scenario 2: Navigation ==="
setup
output=$(run_headless "DOWN
DOWN
UP
q")

f1=$(extract_frame "$output" 1)
check "frame 1 selected=1" "$f1" "selected_index: 1"
f2=$(extract_frame "$output" 2)
check "frame 2 selected=2" "$f2" "selected_index: 2"
f3=$(extract_frame "$output" 3)
check "frame 3 selected=1" "$f3" "selected_index: 1"

echo ""

# ============================================================
echo "=== Scenario 3: Expand directory ==="
setup
output=$(run_headless "e
q")

f1=$(extract_frame "$output" 1)
check "expanded_dirs contains dir_a" "$f1" "dir_a"
check "children: big.txt"            "$f1" "big.txt"
check "children: small.txt"          "$f1" "small.txt"
check "children: sub dir"            "$f1" '\[DIR\]  sub'
check "children indented"            "$f1" '  \[FILE\] big.txt'

echo ""

# ============================================================
echo "=== Scenario 4: Collapse directory ==="
setup
output=$(run_headless "e
e
q")

f1=$(extract_frame "$output" 1)
check "frame 1 has expanded dir_a" "$f1" "dir_a"
f2=$(extract_frame "$output" 2)
check "frame 2 expanded_dirs empty" "$f2" "expanded_dirs: {}"

echo ""

# ============================================================
echo "=== Scenario 5: Delete file (confirm) ==="
setup
output=$(run_headless "DOWN
DOWN
DELETE
y
q")

check "popup confirm_delete"       "$output" "POPUP confirm_delete"
check "popup shows filename"       "$output" 'Delete "root_file.txt"'
check "action deleted logged"      "$output" "ACTION deleted"
last_frame=$(extract_frame "$output" 4)
check_not "root_file.txt gone" "$last_frame" "root_file.txt"

echo ""

# ============================================================
echo "=== Scenario 6: Cancel delete ==="
setup
output=$(run_headless "DOWN
DELETE
n
q")

check "cancel_delete action" "$output" "cancel_delete"

echo ""

# ============================================================
echo "=== Scenario 7: Toggle help ==="
setup
output=$(run_headless "CTRL_H
q")

f0=$(extract_frame "$output" 0)
check "frame 0 show_help: true" "$f0" "show_help: true"
f1=$(extract_frame "$output" 1)
check "frame 1 show_help: false" "$f1" "show_help: false"

echo ""

# ============================================================
echo "=== Scenario 8: Bar color popup ==="
setup
output=$(run_headless "b
q")

check "bar_color popup appears" "$output" "POPUP bar_color"
check "colors listed" "$output" "black, red, green, yellow, blue, magenta, cyan, white"

echo ""

# ============================================================
echo "=== Scenario 9: Navigation boundary ==="
setup
output=$(run_headless "UP
UP
UP
q")

# Every frame should have selected_index: 0
check_not "never goes below 0" "$output" "selected_index: -"

echo ""

# ============================================================
echo "=== Scenario 10: Frame count ==="
setup
output=$(run_headless "DOWN
e
e
q")

frame_count=$(echo "$output" | grep -c "^=== FRAME")
if [ "$frame_count" -eq 5 ]; then
    echo "  PASS: 5 frames (0 initial + 4 events, got $frame_count)"
    PASS=$((PASS + 1))
else
    echo "  FAIL: expected 5 frames, got $frame_count"
    FAIL=$((FAIL + 1))
fi

echo ""
# ============================================================
echo "=== Scenario 11: Pagination with n/p keys ==="
setup_bigdir() {
    cleanup
    local dir="$TEST_DIR/bigdir"
    mkdir -p "$dir"
    for i in $(seq -w 1 50); do
        dd if=/dev/zero of="$dir/file_${i}.txt" bs=1 count=1 2>/dev/null
    done
}

run_headless_bigdir() {
    local events="$1"
    echo "$events" | "$BINARY" "$HEADLESS" "$TEST_DIR/bigdir" 2>&1
}

setup_bigdir
output=$(run_headless_bigdir "q")
f0=$(extract_frame "$output" 0)
check "nav Siguiente appears for 50 files"     "$f0" "Siguiente"
check_not "no Anterior on page 0"               "$f0" "Anterior"
# 30 files + 1 nav = 31 entries, index 30 is nav
check "entry index 30 is nav" "$f0" " 30:.*---.*Siguiente"

# Test n key advances to page 1
setup_bigdir
output=$(run_headless_bigdir "n
q")
f0=$(extract_frame "$output" 0)
f1=$(extract_frame "$output" 1)
check "after n: Anterior present"              "$f1" "Anterior"
check_not "after n: no Siguiente (last page)"   "$f1" "Siguiente"
check "after n: selected on first content"      "$f1" "selected_index: 1"

# Test p key returns to page 0
setup_bigdir
output=$(run_headless_bigdir "n
p
q")
f2=$(extract_frame "$output" 2)
check "after p: Siguiente back"                "$f2" "Siguiente"
check_not "after p: no Anterior"               "$f2" "Anterior"

# Test p on page 0 does nothing (no crash)
setup_bigdir
output=$(run_headless_bigdir "p
q")
f1=$(extract_frame "$output" 1)
check "p on page 0: still page 0" "$f1" "Siguiente"
check_not "p on page 0: no Anterior" "$f1" "Anterior"

echo ""
# ============================================================
cleanup

echo "Results: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
