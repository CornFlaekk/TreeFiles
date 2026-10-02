#!/bin/bash
set -euo pipefail

BINARY="${TREEFILES_BINARY:-./treefiles}"
HEADLESS="--headless"
TEST_DIR="/tmp/treefiles_test_integration"
export TREEFILES_CONFIG="$TEST_DIR/.settings/config.ini"
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
    if grep -q "$pattern" <<< "$output"; then
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
    if grep -q "$pattern" <<< "$output"; then
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

read_frame_from_fd() {
    local input_fd="$1" line
    LAST_FRAME=""
    while IFS= read -r -u "$input_fd" line; do
        LAST_FRAME+="${line}"$'\n'
        if [[ "$line" == "=== END FRAME ===" ]]; then return 0; fi
    done
    return 1
}

run_headless() {
    local events="$1"
    shift
    echo "$events" | "$BINARY" "$HEADLESS" "$@" "$TEST_DIR" 2>&1
}

if [ -z "${TREEFILES_BINARY:-}" ]; then
    echo "Building..."
    make clean > /dev/null 2>&1
    make > /dev/null 2>&1
fi
echo ""

# ============================================================
echo "=== Scenario 1: Initial state ==="
setup
output=$(run_headless "q")

check "frame 0 exists"       "$output" "=== FRAME 0 ==="
check "selected_index: 0"    "$output" "selected_index: 0"
check "scroll_offset: 0"     "$output" "scroll_offset: 0"
check "total_entries listed"  "$output" "total_entries:"
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
echo "=== Scenario 7: Header shows current path ==="
setup
output=$(run_headless "q")

f0=$(extract_frame "$output" 0)
check "frame 0 has current_path" "$f0" "current_path:"
check "frame 0 path is test dir" "$f0" "/tmp/treefiles_test_integration"

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
    shift
    echo "$events" | "$BINARY" "$HEADLESS" "$@" "$TEST_DIR/bigdir" 2>&1
}

setup_bigdir
output=$(run_headless_bigdir "q")
f0=$(extract_frame "$output" 0)
check "nav Next appears for 50 files"     "$f0" "Next"
check_not "no Previous on page 0"               "$f0" "Previous"
# 30 files + 1 nav = 31 entries, index 30 is nav
check "entry index 30 is nav" "$f0" " 30:.*---.*Next"

# Test n key advances to page 1
setup_bigdir
output=$(run_headless_bigdir "n
q")
f0=$(extract_frame "$output" 0)
f1=$(extract_frame "$output" 1)
check "after n: Previous present"              "$f1" "Previous"
check_not "after n: no Next (last page)"   "$f1" "Next"
check "after n: selected on first content"      "$f1" "selected_index: 1"

# Test p key returns to page 0
setup_bigdir
output=$(run_headless_bigdir "n
p
q")
f2=$(extract_frame "$output" 2)
check "after p: Next back"                "$f2" "Next"
check_not "after p: no Previous"               "$f2" "Previous"

# Test p on page 0 does nothing (no crash)
setup_bigdir
output=$(run_headless_bigdir "p
q")
f1=$(extract_frame "$output" 1)
check "p on page 0: still page 0" "$f1" "Next"
check_not "p on page 0: no Previous" "$f1" "Previous"

echo ""
echo "=== Scenario 12: Configurable page size ==="
output=$(run_headless_bigdir "q")
f0=$(extract_frame "$output" 0)
check "default page size remains 30" "$f0" "page_size: 30"
check "default total page count" "$f0" "pagination: page 1 of 2"

output=$(run_headless_bigdir $'n\np\nq' --page-size 7)
f0=$(extract_frame "$output" 0)
f1=$(extract_frame "$output" 1)
f2=$(extract_frame "$output" 2)
check "custom first page: seven files and next" "$f0" "total_entries: 8"
check "custom total page count" "$f0" "pagination: page 1 of 8"
check_not "custom first page stops after seven files" "$f0" "file_08.txt"
check "custom middle page: seven files and two markers" "$f1" "total_entries: 9"
check "next advances custom page" "$f1" "pagination: page 2 of 8"
check "custom next selects first file" "$f1" '>>> \[FILE\] file_08.txt'
check "custom previous selects last file" "$f2" '>>> \[FILE\] file_07.txt'

output=$(run_headless_bigdir $'n\nn\nn\nn\nn\nn\nn\nn\np\nq' --page-size=7)
f7=$(extract_frame "$output" 7)
f8=$(extract_frame "$output" 8)
f9=$(extract_frame "$output" 9)
check "last custom page has one file and previous" "$f7" "total_entries: 2"
check "equals option reaches last page" "$f7" "pagination: page 8 of 8"
check "next stops at last custom page" "$f8" "pagination: page 8 of 8"
check "previous from last page selects prior last file" "$f9" '>>> \[FILE\] file_49.txt'

output=$(run_headless_bigdir $'n\np\nq' --page-size 1)
f0=$(extract_frame "$output" 0)
f1=$(extract_frame "$output" 1)
f2=$(extract_frame "$output" 2)
check "one entry per page" "$f0" "pagination: page 1 of 50"
check "one-entry page selection" "$f1" '>>> \[FILE\] file_02.txt'
check "one-entry previous selection" "$f2" '>>> \[FILE\] file_01.txt'
for size in 50 2147483647; do
    output=$(run_headless_bigdir "q" --page-size "$size")
    f0=$(extract_frame "$output" 0)
    check "page size $size includes all files" "$f0" "total_entries: 50"
    check_not "page size $size needs no next marker" "$f0" "Next"
done

output=$(run_headless $'e\nn\nq' --page-size 7)
f1=$(extract_frame "$output" 1)
f2=$(extract_frame "$output" 2)
check "expanded directory has independent custom page" "$f1" "total_entries: 9"
check_not "expanded directory respects custom limit" "$f1" "file_08.txt"
check "next navigates expanded child page" "$f2" '>>>   \[FILE\] file_08.txt'

echo ""
echo "=== Scenario 13: CLI validation ==="
for arguments in "--page-size" "--page-size=" "--page-size 0" "--page-size -1" \
                 "--page-size abc" "--page-size 7x" "--page-size 1.5" \
                 "--page-size 2147483648" "--page-size 99999999999999999999"; do
    read -r -a options <<< "$arguments"
    if output=$("$BINARY" --headless "${options[@]}" </dev/null 2>&1); then
        status=0
    else
        status=$?
    fi
    check "invalid argument exit: $arguments" "$status" '^2$'
    check "invalid argument message: $arguments" "$output" 'positive integer'
    check_not "invalid argument does not scan: $arguments" "$output" '=== FRAME'
done
output=$("$BINARY" --help)
check "CLI help documents page size" "$output" 'page-size N'
output=$("$BINARY" --version)
check "CLI reports the release version" "$output" 'TreeFiles 0.1.0'
if output=$("$BINARY" --headless --page-sze 7 </dev/null 2>&1); then
    status=0
else
    status=$?
fi
check "unknown option exit" "$status" '^2$'
check "unknown option message" "$output" 'Unknown option'

echo ""
echo "=== Scenario 14: Vim navigation ==="
setup
output=$(run_headless $'j\nk\nk\nG\ng\nq')
check "Vim j moves down" "$(extract_frame "$output" 1)" 'selected_index: 1'
check "Vim k stops at first entry" "$(extract_frame "$output" 3)" 'selected_index: 0'
check "Vim G selects last entry" "$(extract_frame "$output" 4)" 'selected_index: 2'
check "Vim g selects first entry" "$(extract_frame "$output" 5)" 'selected_index: 0'
output=$(run_headless $'l\nl\nj\nh\nq')
check "Vim l expands directory" "$(extract_frame "$output" 1)" 'big.txt'
check "Vim l keeps directory open" "$(extract_frame "$output" 2)" 'big.txt'
check_not "Vim h from child folds parent" "$(extract_frame "$output" 4)" 'big.txt'
check "Vim h selects folded parent" "$(extract_frame "$output" 4)" 'selected_index: 0'
output=$(run_headless $'RIGHT\nLEFT\nq')
check "right arrow expands" "$(extract_frame "$output" 1)" 'big.txt'
check_not "left arrow folds" "$(extract_frame "$output" 2)" 'big.txt'
mkdir -p "$TEST_DIR/empty"
output=$(printf 'j\nk\nh\nl\ng\nG\nq\n' | "$BINARY" --headless "$TEST_DIR/empty")
check "Vim keys are safe on empty directory" "$(extract_frame "$output" 7)" 'total_entries: 0'

echo ""
echo "=== Scenario 15: Refresh external changes ==="
refresh_root="$TEST_DIR/refresh-root"
mkdir -p "$refresh_root/sub"
printf x > "$refresh_root/a.txt"
printf xx > "$refresh_root/b.txt"
printf x > "$refresh_root/sub/deep.txt"
coproc REFRESH_PROC { "$BINARY" --headless "$refresh_root"; }
read_frame_from_fd "${REFRESH_PROC[0]}"
printf 'DOWN\n' >&"${REFRESH_PROC[1]}"
read_frame_from_fd "${REFRESH_PROC[0]}"
printf 'DOWN\n' >&"${REFRESH_PROC[1]}"
read_frame_from_fd "${REFRESH_PROC[0]}"
printf 'e\n' >&"${REFRESH_PROC[1]}"
read_frame_from_fd "${REFRESH_PROC[0]}"
printf 'UP\n' >&"${REFRESH_PROC[1]}"
read_frame_from_fd "${REFRESH_PROC[0]}"
printf 'abcdefghijkl' > "$refresh_root/a.txt"
printf '12345678901234567890' > "$refresh_root/sub/deep.txt"
printf '123456' > "$refresh_root/added.txt"
mv "$refresh_root/b.txt" "$refresh_root/renamed.txt"
printf 'REFRESH\n' >&"${REFRESH_PROC[1]}"
read_frame_from_fd "${REFRESH_PROC[0]}"
refresh_frame="$LAST_FRAME"
check "refresh preserves valid expanded directories" "$refresh_frame" 'deep.txt'
check "refresh sees external create and rename" "$refresh_frame" 'added.txt'
check "refresh removes externally renamed old path" "$refresh_frame" 'renamed.txt'
check_not "refresh removes the old name" "$refresh_frame" 'b.txt'
check "refresh restores selection by path after reordering" "$refresh_frame" ' 2: >>> \[FILE\] a.txt'
rm "$refresh_root/added.txt"
printf 'r\n' >&"${REFRESH_PROC[1]}"
read_frame_from_fd "${REFRESH_PROC[0]}"
check_not "refresh sees external deletion" "$LAST_FRAME" 'added.txt'
check "refresh keeps a valid selection after deletion" "$LAST_FRAME" 'a.txt'
printf 'q\n' >&"${REFRESH_PROC[1]}"
wait "$REFRESH_PROC_PID"
check "refresh session exits cleanly" "$?" '^0$'

page_root="$TEST_DIR/refresh-pages"
mkdir -p "$page_root"
for name in one.txt two.txt three.txt; do printf x > "$page_root/$name"; done
coproc PAGE_PROC { "$BINARY" --headless --page-size 1 "$page_root"; }
read_frame_from_fd "${PAGE_PROC[0]}"
printf 'n\n' >&"${PAGE_PROC[1]}"
read_frame_from_fd "${PAGE_PROC[0]}"
printf 'n\n' >&"${PAGE_PROC[1]}"
read_frame_from_fd "${PAGE_PROC[0]}"
rm "$page_root/two.txt" "$page_root/three.txt"
printf 'R\n' >&"${PAGE_PROC[1]}"
read_frame_from_fd "${PAGE_PROC[0]}"
check_not "refresh removes stale Previous marker" "$LAST_FRAME" 'Previous'
check_not "refresh removes stale Next marker" "$LAST_FRAME" 'Next'
check "refresh clamps a deleted last-page selection" "$LAST_FRAME" 'selected_index: 0'
printf 'q\n' >&"${PAGE_PROC[1]}"
wait "$PAGE_PROC_PID"
check "last-page refresh session exits cleanly" "$?" '^0$'

echo ""
echo "=== Scenario 16: Language selection ==="
setup_bigdir
output=$(run_headless_bigdir "q")
check "English is the default language" "$(extract_frame "$output" 0)" 'language: en'
check "default navigation is English" "$(extract_frame "$output" 0)" 'Next'
output=$(run_headless_bigdir "q" --lang=es)
check "Spanish language option" "$(extract_frame "$output" 0)" 'language: es'
check "Spanish navigation is translated" "$(extract_frame "$output" 0)" 'Siguiente'
output=$("$BINARY" --help --lang es)
check "help follows language regardless of option order" "$output" 'Uso:'
for argument in "--lang" "--lang=" "--lang=fr"; do
    if output=$("$BINARY" "$argument" </dev/null 2>&1); then status=0; else status=$?; fi
    check "invalid language: $argument" "$status" '^2$'
done

echo ""
echo "=== Scenario 16: Persistent colors ==="
output=$(run_headless_bigdir $'COLOR red blue\nq')
check "color selection saves configuration" "$output" 'ACTION colors_saved'
check "foreground applies immediately" "$(extract_frame "$output" 1)" 'bar_fg: 1'
check "background applies immediately" "$(extract_frame "$output" 1)" 'bar_bg: 4'
output=$(run_headless_bigdir "q")
check "foreground survives restart" "$(extract_frame "$output" 0)" 'bar_fg: 1'
check "background survives restart" "$(extract_frame "$output" 0)" 'bar_bg: 4'
output=$(run_headless_bigdir $'COLOR white black\nq')
output=$(run_headless_bigdir $'COLOR purple blue\nq')
check "new foreground replaces old config" "$(extract_frame "$output" 0)" 'bar_fg: 7'
check "new background replaces old config" "$(extract_frame "$output" 0)" 'bar_bg: 0'
check "invalid color reports an error" "$output" 'Invalid COLOR event'
check "invalid color leaves foreground unchanged" "$(extract_frame "$output" 1)" 'bar_fg: 7'
check "invalid color leaves background unchanged" "$(extract_frame "$output" 1)" 'bar_bg: 0'
printf 'foreground=invalid\nbackground=cyan\n' > "$TREEFILES_CONFIG"
output=$(run_headless_bigdir "q")
check "invalid saved color falls back" "$(extract_frame "$output" 0)" 'bar_fg: 0'
check "valid saved field is preserved" "$(extract_frame "$output" 0)" 'bar_bg: 6'

echo ""
echo "=== Scenario 17: Scan diagnostics and safe links ==="
setup
output=$(run_headless $'W\nq')
check "headless W opens scan diagnostics" "$output" "POPUP scan_diagnostics"
check "complete scan status is included in frames" "$(extract_frame "$output" 0)" "scan_status: complete"
mkdir -p "$TEST_DIR/link-target"
printf 'target data' > "$TEST_DIR/link-target/keep.txt"
if ln -s "$TEST_DIR/link-target" "$TEST_DIR/target-link"; then
    output=$(run_headless $'G\nDELETE\ny\nq')
    check "link deletion action is logged" "$output" "ACTION deleted"
    if [ -f "$TEST_DIR/link-target/keep.txt" ]; then
        echo "  PASS: deleting a directory link keeps its target"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: deleting a directory link removed its target"
        FAIL=$((FAIL + 1))
    fi
else
    echo "  SKIP: this filesystem cannot create directory symlinks"
fi

echo ""
# ============================================================
cleanup

echo "Results: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
