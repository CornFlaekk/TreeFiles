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
echo "=== Scenario 16: Root navigation ==="
nav_root="$TEST_DIR/navigation-ñ"
space_name="folder with spaces ñ"
mkdir -p "$nav_root/sub" "$nav_root/$space_name"
printf '12345678901234567890' > "$nav_root/sub/deep.txt"
printf xx > "$nav_root/$space_name/inside.txt"
printf x > "$nav_root/root.txt"
output=$(printf 'ENTER\nBACKSPACE\nCD %s\nCD missing directory\nBACKSPACE\nENTER\nENTER\nq\n' "$space_name" | "$BINARY" --headless "$nav_root")
check "Enter changes root to the selected directory" "$(extract_frame "$output" 1)" "current_path: $nav_root/sub"
check "Backspace returns to the parent root" "$(extract_frame "$output" 2)" "current_path: $nav_root"
check "Backspace selects the directory returned from" "$(extract_frame "$output" 2)" '>>> \[DIR\]  sub'
check "CD accepts a relative Unicode path with spaces" "$(extract_frame "$output" 3)" "current_path: $nav_root/$space_name"
check "invalid CD reports a path error" "$output" 'POPUP navigation_error'
check "failed CD keeps the previous root" "$(extract_frame "$output" 4)" "current_path: $nav_root"
check "Enter reopens the selected directory" "$(extract_frame "$output" 5)" "current_path: $nav_root/$space_name"
check "Enter on a file does not launch or navigate" "$(extract_frame "$output" 6)" "current_path: $nav_root/$space_name"

echo ""
echo "=== Scenario 17: Language selection ==="
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
echo "=== Scenario 18: Persistent colors ==="
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
echo "=== Scenario 19: Scan diagnostics and safe links ==="
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
echo "=== Scenario 20: Sorting ==="
setup_bigdir
sort_child="$TEST_DIR/bigdir/sub"
mkdir -p "$sort_child"
: > "$sort_child/child-a.txt"
: > "$sort_child/child-z.txt"
output=$(run_headless_bigdir $'n\nSORT name desc\nREFRESH\nCD '"$sort_child"$'\nBACKSPACE\nS\nT\nq' --page-size=2)
check "default sort state is size descending" "$(extract_frame "$output" 0)" 'sort_key: size'
check "default sort direction is descending" "$(extract_frame "$output" 0)" 'sort_order: desc'
check "pagination reaches the second page before sorting" "$(extract_frame "$output" 1)" 'file_03.txt'
check "sort change resets selection to first result" "$(extract_frame "$output" 2)" 'selected_index: 0'
check "sort change resets scroll" "$(extract_frame "$output" 2)" 'scroll_offset: 0'
check "sort before pagination changes first-page membership" "$(extract_frame "$output" 2)" 'file_50.txt'
check "refresh keeps selected sort options" "$(extract_frame "$output" 3)" 'sort_key: name'
check "navigation keeps selected sort options" "$(extract_frame "$output" 4)" 'sort_order: desc'
check "return to parent keeps selected sort options" "$(extract_frame "$output" 5)" 'current_path:'
check "S cycles the sort key" "$(extract_frame "$output" 6)" 'sort_key: mtime'
check "T toggles the sort direction" "$(extract_frame "$output" 7)" 'sort_order: asc'
output=$(run_headless_bigdir "q" --sort=name --order=asc)
check "CLI equals forms select name ascending" "$(extract_frame "$output" 0)" 'sort_key: name'
check "CLI equals form orders names ascending" "$(extract_frame "$output" 0)" 'sort_order: asc'
check "CLI name sort picks the first name" "$(extract_frame "$output" 0)" 'file_01.txt'
for argument in "--sort" "--sort=type" "--order" "--order=sideways"; do
    if output=$("$BINARY" "$argument" 2>&1); then status=0; else status=$?; fi
    check "invalid sort option rejected before curses: $argument" "$status" '^2$'
done

echo "=== Scenario 21: Persistent preferences ==="
setup_bigdir
output=$(run_headless_bigdir "q" --lang=es --page-size=7 --save-settings)
check "save-settings persists language" "$(extract_frame "$output" 0)" 'language: es'
check "save-settings persists page size" "$(extract_frame "$output" 0)" 'page_size: 7'
check "config includes saved language" "$(cat "$TREEFILES_CONFIG")" '^language=es$'
check "config includes saved page size" "$(cat "$TREEFILES_CONFIG")" '^page_size=7$'
output=$(run_headless_bigdir "q")
check "saved preferences load in a later process" "$(extract_frame "$output" 0)" 'language: es'
check "saved page size loads in a later process" "$(extract_frame "$output" 0)" 'page_size: 7'
output=$(run_headless_bigdir "q" --lang=en --page-size=2)
check "CLI language overrides saved value for this session" "$(extract_frame "$output" 0)" 'language: en'
check "CLI page size overrides saved value for this session" "$(extract_frame "$output" 0)" 'page_size: 2'
check "session overrides leave saved language intact" "$(cat "$TREEFILES_CONFIG")" '^language=es$'
check "session overrides leave saved page size intact" "$(cat "$TREEFILES_CONFIG")" '^page_size=7$'
output=$(run_headless_bigdir $'COLOR red blue\nq' --lang=en --page-size=2)
check "color selection applies with CLI preference overrides" "$(extract_frame "$output" 1)" 'bar_fg: 1'
check "color selection updates the saved foreground" "$(cat "$TREEFILES_CONFIG")" '^foreground=red$'
check "color save preserves saved language" "$(cat "$TREEFILES_CONFIG")" '^language=es$'
check "color save preserves saved page size" "$(cat "$TREEFILES_CONFIG")" '^page_size=7$'
check "color selection updates the saved background" "$(cat "$TREEFILES_CONFIG")" '^background=blue$'
output=$(run_headless_bigdir "q")
check "all preferences reload together" "$(extract_frame "$output" 0)" 'language: es'
check "saved page size and colors reload together" "$(extract_frame "$output" 0)" 'page_size: 7'
check "saved foreground reloads" "$(extract_frame "$output" 0)" 'bar_fg: 1'
check "saved background reloads" "$(extract_frame "$output" 0)" 'bar_bg: 4'

persisted_hash=$(sha256sum "$TREEFILES_CONFIG" | cut -d' ' -f1)
output=$("$BINARY" --help --save-settings --lang en --page-size 4 2>&1)
status=$?
check "help succeeds without saving settings" "$status" '^0$'
check "help leaves config unchanged" "$(sha256sum "$TREEFILES_CONFIG" | cut -d' ' -f1)" "^$persisted_hash$"
output=$("$BINARY" --version --save-settings --lang en --page-size 4 2>&1)
status=$?
check "version succeeds without saving settings" "$status" '^0$'
check "version leaves config unchanged" "$(sha256sum "$TREEFILES_CONFIG" | cut -d' ' -f1)" "^$persisted_hash$"
if output=$("$BINARY" --save-settings --page-size 0 "$TEST_DIR/bigdir" 2>&1); then status=0; else status=$?; fi
check "invalid option is rejected" "$status" '^2$'
check "invalid option leaves config unchanged" "$(sha256sum "$TREEFILES_CONFIG" | cut -d' ' -f1)" "^$persisted_hash$"
if output=$("$BINARY" --save-settings --lang en --page-size 4 "$TEST_DIR/missing-root" 2>&1); then status=0; else status=$?; fi
check "invalid root is rejected" "$status" '^1$'
check "invalid root leaves config unchanged" "$(sha256sum "$TREEFILES_CONFIG" | cut -d' ' -f1)" "^$persisted_hash$"
old_config=$TREEFILES_CONFIG
TREEFILES_CONFIG="$TEST_DIR/help-must-not-create/config.ini" "$BINARY" --help --save-settings >/dev/null 2>&1
check "help does not create a new config file" "$(test -e "$TEST_DIR/help-must-not-create/config.ini"; echo $?)" '^1$'
export TREEFILES_CONFIG=$old_config

mkdir -p "$TEST_DIR/blocked.ini"
printf preserve > "$TEST_DIR/blocked.ini/keep.txt"
if output=$(TREEFILES_CONFIG="$TEST_DIR/blocked.ini" "$BINARY" --save-settings --lang es --page-size 7 "$TEST_DIR/bigdir" 2>&1); then status=0; else status=$?; fi
check "failed atomic save returns an error" "$status" '^1$'
check "failed atomic save preserves existing target" "$(cat "$TEST_DIR/blocked.ini/keep.txt")" '^preserve$'
check "failed preference save removes temporary files" "$(find "$TEST_DIR" -maxdepth 1 -name 'blocked.ini.tmp.*' -print)" '^$'
export TREEFILES_CONFIG=$old_config

echo ""
echo "=== Scenario 22: Filename and extension filters ==="
filter_root="$TEST_DIR/filter-root"
filter_context="$filter_root/context"
mkdir -p "$filter_context"
printf '12345678' > "$filter_root/Annual Report.TXT"
printf '1234' > "$filter_root/report.csv"
printf '123' > "$filter_root/other.txt"
printf '12345678901234567890' > "$filter_context/deep-report.log"
printf '1234567890' > "$filter_context/unmatched.bin"
output=$(printf 'e\nFILTER annual report\nENTER\nBACKSPACE\nEXT .TXT\nREFRESH\nFILTER missing\nCLEAR_FILTER\nq\n' | "$BINARY" --headless "$filter_root")
f2=$(extract_frame "$output" 2)
check "FILTER preserves spaces and matches filename case-insensitively" "$f2" 'filter_text: annual report'
check "name filter counts only matching files" "$f2" 'matching_files: 1'
check "name filter retains context directories and matching file" "$f2" 'Annual Report.TXT'
check "filter resets selection and scroll" "$f2" 'selected_index: 0'
f3=$(extract_frame "$output" 3)
check "Enter preserves filters while changing root" "$f3" "current_path: $filter_context"
f4=$(extract_frame "$output" 4)
check "Backspace preserves filters" "$f4" 'filter_text: annual report'
f5=$(extract_frame "$output" 5)
check "EXT combines filters and ignores extension case" "$f5" 'filter_extension: .TXT'
check "combined filters count the one matching file" "$f5" 'matching_files: 1'
f6=$(extract_frame "$output" 6)
check "refresh keeps filters" "$f6" 'filter_extension: .TXT'
f7=$(extract_frame "$output" 7)
check "zero matches retain the context directory" "$f7" 'context'
check "zero matches are counted" "$f7" 'matching_files: 0'
check_not "zero matches have no phantom next page" "$f7" 'Next'
f8=$(extract_frame "$output" 8)
check "CLEAR_FILTER clears filename and extension" "$f8" 'filter_extension: '
check "clearing restores root files" "$f8" 'matching_files: 3'
output=$(printf 'q\n' | "$BINARY" --headless --filter "Annual Report" --ext=.TXT "$filter_root")
check "CLI preserves filters containing spaces and original case" "$(extract_frame "$output" 0)" 'filter_text: Annual Report'
check "CLI reports canonical matching count" "$(extract_frame "$output" 0)" 'matching_files: 1'
for argument in filter ext; do
    if output=$("$BINARY" --headless "--$argument" 2>&1); then status=0; else status=$?; fi
    check "missing filter value rejected: $argument" "$status" '^2$'
done
output=$(printf 'FILTER\nEXT\nq\n' | "$BINARY" --headless --lang=en "$filter_root")
check "bare headless filter events report an error" "$output" 'Invalid filter event'

echo ""
echo "=== Scenario 22: JSON and CSV export ==="
export_root="$TEST_DIR/export root, raíz"
export_context="$export_root/context"
mkdir -p "$export_context"
printf '123456789012' > "$export_root/alpha, ñ.txt"
printf '12345678901234567890' > "$export_root/beta.txt"
printf '12345' > "$export_context/deep.txt"
weird_name=$'comma,"quoted"\nline\\part.txt'
printf 'special' > "$export_root/$weird_name"
mkdir -p "$(dirname "$TREEFILES_CONFIG")"
printf 'foreground=cyan\nbackground=blue\n' > "$TREEFILES_CONFIG"
config_before=$(sha256sum "$TREEFILES_CONFIG" | cut -d' ' -f1)
json_export=$("$BINARY" --export=json --page-size=1 --sort=name --order=asc "$export_root" 2>"$TEST_DIR/export.stderr")
if [ -s "$TEST_DIR/export.stderr" ]; then
    echo "  FAIL: JSON stdout is not mixed with diagnostics"
    FAIL=$((FAIL + 1))
else
    echo "  PASS: JSON stdout is not mixed with diagnostics"
    PASS=$((PASS + 1))
fi
if printf '%s' "$json_export" | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["schema_version"] == 1 and d["complete"]; assert len(d["entries"]) == 4; assert d["entries"][0]["path"] == "alpha, ñ.txt"; assert all(e["depth"] == 0 for e in d["entries"]); assert next(e for e in d["entries"] if e["type"] == "directory")["size_bytes"] == 5; assert any(e["path"] == "comma,\"quoted\"\nline\\part.txt" for e in d["entries"])'; then
    echo "  PASS: JSON parser verifies schema, Unicode/control escaping, direct children, aggregate size, and no page truncation"
    PASS=$((PASS + 1))
else
    echo "  FAIL: JSON report did not satisfy the schema"
    FAIL=$((FAIL + 1))
fi
filtered_export=$("$BINARY" --export json --page-size 1 --filter alpha --ext=.TXT --lang=es "$export_root")
if printf '%s' "$filtered_export" | python3 -c 'import json,sys; d=json.load(sys.stdin); assert len(d["entries"]) == 2; assert d["entries"][0]["path"] == "alpha, ñ.txt"; assert d["entries"][1]["type"] == "directory"; assert d["sort"]["key"] == "size"; assert d["filter"]["extension"] == ".TXT"'; then
    echo "  PASS: JSON export applies shared filters and keeps directory context"
    PASS=$((PASS + 1))
else
    echo "  FAIL: JSON export filter semantics are incorrect"
    FAIL=$((FAIL + 1))
fi
json_english=$("$BINARY" --export json --lang en "$export_root")
json_spanish=$("$BINARY" --export json --lang es "$export_root")
if python3 -c 'import json,sys; a=json.loads(sys.argv[1]); b=json.loads(sys.argv[2]); assert list(a.keys()) == list(b.keys()); assert [e["type"] for e in a["entries"]] == [e["type"] for e in b["entries"]]' "$json_english" "$json_spanish"; then
    echo "  PASS: JSON schema and canonical types are language-independent"
    PASS=$((PASS + 1))
else
    echo "  FAIL: JSON schema changed with the interface language"
    FAIL=$((FAIL + 1))
fi
csv_path="$TEST_DIR/export report.csv"
"$BINARY" --export=csv --output="$csv_path" --page-size=1 --sort=name --order=asc "$export_root" 2>"$TEST_DIR/csv.stderr"
if [ ! -s "$TEST_DIR/csv.stderr" ] && python3 -c 'import csv,sys; rows=list(csv.DictReader(open(sys.argv[1],encoding="utf-8",newline=""))); assert len(rows)==4; assert rows[0]["path"]=="alpha, ñ.txt"; assert all(r["scan_complete"]=="true" and r["depth"]=="0" for r in rows); assert next(r for r in rows if r["type"]=="directory")["size_bytes"]=="5"; assert any(r["path"]=="comma,\"quoted\"\nline\\part.txt" for r in rows)' "$csv_path"; then
    echo "  PASS: Python CSV reader verifies quoted commas, quotes, backslashes, newlines, Unicode, and all direct children"
    PASS=$((PASS + 1))
else
    echo "  FAIL: CSV report did not satisfy the schema"
    FAIL=$((FAIL + 1))
fi
config_after=$(sha256sum "$TREEFILES_CONFIG" | cut -d' ' -f1)
check "export does not alter saved settings" "$config_after" "$config_before"

for expected in 'yaml' 'missing-format' 'csv-no-output' 'output-without-export' 'headless' 'save-settings'; do
    case "$expected" in
        yaml) args=(--export yaml "$export_root") ;;
        missing-format) args=(--export) ;;
        csv-no-output) args=(--export csv "$export_root") ;;
        output-without-export) args=(--output "$csv_path" "$export_root") ;;
        headless) args=(--export json --headless "$export_root") ;;
        save-settings) args=(--export json --save-settings "$export_root") ;;
    esac
    if "$BINARY" "${args[@]}" >/dev/null 2>"$TEST_DIR/invalid-export.stderr"; then status=0; else status=$?; fi
    check "invalid export arguments return code 2: $expected" "$status" '^2$'
done

missing_root="$TEST_DIR/missing export root"
if output=$("$BINARY" --export json "$missing_root" 2>"$TEST_DIR/missing-root.stderr"); then status=0; else status=$?; fi
if [ "$status" -eq 1 ] && printf '%s' "$output" | python3 -c 'import json,sys; d=json.load(sys.stdin); assert not d["complete"] and len(d["diagnostics"]) > 0'; then
    echo "  PASS: missing root returns valid diagnostic JSON and exit code 1"
    PASS=$((PASS + 1))
else
    echo "  FAIL: missing root export response is incorrect"
    FAIL=$((FAIL + 1))
fi
preserved="$TEST_DIR/preserved.csv"
printf 'previous report' > "$preserved"
if "$BINARY" --export csv --output "$preserved" "$missing_root" >/dev/null 2>"$TEST_DIR/missing-csv.stderr"; then status=0; else status=$?; fi
check "scan failure preserves a prior CSV file" "$status" '^1$'
check "preserved CSV content is unchanged" "$(cat "$preserved")" '^previous report$'

blocked="$TEST_DIR/blocked-output.csv"
mkdir -p "$blocked"
printf preserve > "$blocked/keep.txt"
if "$BINARY" --export csv --output "$blocked" "$export_root" >/dev/null 2>"$TEST_DIR/write-error.stderr"; then status=0; else status=$?; fi
check "output replacement failure returns code 1" "$status" '^1$'
check "failed replacement leaves existing destination contents intact" "$(cat "$blocked/keep.txt")" '^preserve$'
if find "$TEST_DIR" -maxdepth 1 -name '.treefiles-tmp-*' -print -quit | grep -q .; then
    echo "  FAIL: failed export left a temporary file"
    FAIL=$((FAIL + 1))
else
    echo "  PASS: failed export cleans its temporary file"
    PASS=$((PASS + 1))
fi
empty_root="$TEST_DIR/empty export root"
mkdir -p "$empty_root"
empty_json=$("$BINARY" --export json "$empty_root")
if printf '%s' "$empty_json" | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["complete"] and d["entries"]==[]'; then
    echo "  PASS: empty root exports a valid empty JSON report"
    PASS=$((PASS + 1))
else
    echo "  FAIL: empty root JSON report is incorrect"
    FAIL=$((FAIL + 1))
fi
empty_csv="$TEST_DIR/empty.csv"
"$BINARY" --export csv --output "$empty_csv" "$empty_root"
if python3 -c 'import csv,sys; f=open(sys.argv[1],encoding="utf-8",newline=""); reader=csv.DictReader(f); assert reader.fieldnames==["root","path","type","size_bytes","depth","size_status","scan_complete"]; assert list(reader)==[]' "$empty_csv"; then
    echo "  PASS: empty CSV keeps its schema header with no data rows"
    PASS=$((PASS + 1))
else
    echo "  FAIL: empty CSV report is incorrect"
    FAIL=$((FAIL + 1))
fi

echo ""
# ============================================================
cleanup

echo "Results: $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
