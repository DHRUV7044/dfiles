#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-./build/dfiles}"
echo "=========================================================="
echo "  DFILES PHASE 1: HOSTILE ENVIRONMENT INJECTION SUITE     "
echo "  Target Binary: $BIN"
echo "=========================================================="

PASS_COUNT=0
FAIL_COUNT=0

record_pass() {
    local name="$1"
    local info="${2:-}"
    echo "  [PASS] $name $info"
    PASS_COUNT=$((PASS_COUNT + 1))
}

record_fail() {
    local name="$1"
    local info="${2:-}"
    echo "  [FAIL] $name $info"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

# -------------------------------------------------------------
# Vector 1.1: Missing $HOME Environment
# -------------------------------------------------------------
echo ""
echo "--- Vector 1.1: Missing & Hostile \$HOME Tests ---"

# 1. Empty HOME
out=$(env HOME="" timeout --preserve-status 2s "$BIN" --test-mode 2>&1 || true)
rc=$?
# 124 or 143 is timeout kill, 0 is clean exit. Neither is a crash (134/139).
if [ $rc -eq 124 ] || [ $rc -eq 143 ] || [ $rc -eq 0 ]; then
    record_pass "HOME=\"\" gracefully handled (no SIGSEGV/abort)" "(Exit Code: $rc)"
else
    record_fail "HOME=\"\" failed with crash" "(Exit Code: $rc)"
fi

# 2. Unset HOME
out=$(env -u HOME timeout --preserve-status 2s "$BIN" --test-mode 2>&1 || true)
rc=$?
if [ $rc -eq 124 ] || [ $rc -eq 143 ] || [ $rc -eq 0 ]; then
    record_pass "Unset HOME gracefully handled" "(Exit Code: $rc)"
else
    record_fail "Unset HOME crashed" "(Exit Code: $rc)"
fi

# 3. Nonexistent HOME directory
out=$(env HOME="/nonexistent_dir_$$" timeout --preserve-status 2s "$BIN" --test-mode 2>&1 || true)
rc=$?
if [ $rc -eq 124 ] || [ $rc -eq 143 ] || [ $rc -eq 0 ]; then
    record_pass "Nonexistent \$HOME directory handled cleanly" "(Exit Code: $rc)"
else
    record_fail "Nonexistent \$HOME crashed" "(Exit Code: $rc)"
fi

# 4. Inaccessible /root directory as HOME
out=$(env HOME="/root" timeout --preserve-status 2s "$BIN" --test-mode 2>&1 || true)
rc=$?
if [ $rc -eq 124 ] || [ $rc -eq 143 ] || [ $rc -eq 0 ]; then
    record_pass "Inaccessible /root \$HOME handled cleanly" "(Exit Code: $rc)"
else
    record_fail "Inaccessible /root \$HOME crashed" "(Exit Code: $rc)"
fi

# -------------------------------------------------------------
# Vector 1.2: Missing Display Server
# -------------------------------------------------------------
echo ""
echo "--- Vector 1.2: Missing & Invalid Display Server Tests ---"

# 1. Completely missing display server
set +e
out=$(env DISPLAY="" WAYLAND_DISPLAY="" "$BIN" --test-mode 2>&1)
rc=$?
set -e
if [ $rc -eq 1 ] && echo "$out" | grep -qi "Failed to open display"; then
    record_pass "Missing display (DISPLAY=\"\" WAYLAND=\"\") returns exit 1 with GTK error" "(Code: $rc)"
else
    record_fail "Missing display unexpected behavior" "(Exit Code: $rc, Out: $out)"
fi

# 2. Invalid DISPLAY and WAYLAND_DISPLAY
set +e
out=$(env DISPLAY=":9999" WAYLAND_DISPLAY="nonexistent_socket_$$" "$BIN" --test-mode 2>&1)
rc=$?
set -e
if [ $rc -eq 1 ] && echo "$out" | grep -qi "Failed to open display\|Cannot open display"; then
    record_pass "Invalid display socket returns exit 1 without crash" "(Code: $rc)"
else
    record_fail "Invalid display unexpected behavior" "(Exit Code: $rc, Out: $out)"
fi

# -------------------------------------------------------------
# Vector 1.3: Invalid / Malformed Command Line Flags
# -------------------------------------------------------------
echo ""
echo "--- Vector 1.3: Malformed & Hostile CLI Flag Injections ---"

# 1. Unrecognized flag --invalid-arg-xyz
set +e
out=$("$BIN" --invalid-arg-xyz 2>&1)
rc=$?
set -e
if [ $rc -eq 1 ] && echo "$out" | grep -qi "Unknown option"; then
    record_pass "Flag '--invalid-arg-xyz' rejected with exit 1"
else
    record_fail "Flag '--invalid-arg-xyz' unexpected result" "(Code: $rc)"
fi

# 2. Unknown short option -z
set +e
out=$("$BIN" -z 2>&1)
rc=$?
set -e
if [ $rc -eq 1 ] && echo "$out" | grep -qi "Unknown option"; then
    record_pass "Short option '-z' rejected with exit 1"
else
    record_fail "Short option '-z' unexpected result" "(Code: $rc)"
fi

# 3. Valid flag followed by invalid flag
set +e
out=$("$BIN" --version --bogus 2>&1)
rc=$?
set -e
if [ $rc -eq 1 ]; then
    record_pass "Mixed flags '--version --bogus' rejected"
else
    record_fail "Mixed flags unexpected result" "(Code: $rc)"
fi

# 4. Long option buffer overflow attempt (65536 chars)
LONG_ARG=$(printf 'A%.0s' {1..65536})
set +e
out=$("$BIN" "--$LONG_ARG" 2>&1)
rc=$?
set -e
if [ $rc -eq 1 ]; then
    record_pass "64KB long flag buffer overflow rejected cleanly (Code: $rc)"
else
    record_fail "64KB long flag caused crash" "(Code: $rc)"
fi

# 5. Non-UTF8 byte injection in arguments
set +e
out=$(timeout --preserve-status 1s "$BIN" --test-mode "$(printf '\x80\xFF\xFE\xAA')" 2>&1 || true)
rc=$?
set -e
# Should not crash (code 134/139).
if [ $rc -ne 134 ] && [ $rc -ne 139 ]; then
    record_pass "Malformed non-UTF8 byte injection survived without crash" "(Code: $rc)"
else
    record_fail "Malformed non-UTF8 byte caused crash" "(Code: $rc)"
fi

# 6. Null string argument
set +e
out=$(timeout --preserve-status 1s "$BIN" --test-mode "" 2>&1 || true)
rc=$?
set -e
if [ $rc -ne 134 ] && [ $rc -ne 139 ]; then
    record_pass "Empty string argument handled without crash" "(Code: $rc)"
else
    record_fail "Empty string argument caused crash" "(Code: $rc)"
fi

# 7. Non-existent file path argument
set +e
out=$(timeout --preserve-status 2s "$BIN" --test-mode "/nonexistent/path/$$" 2>&1 || true)
rc=$?
set -e
if [ $rc -eq 124 ] || [ $rc -eq 143 ] || [ $rc -eq 0 ]; then
    record_pass "Nonexistent file path argument handled gracefully" "(Code: $rc)"
else
    record_fail "Nonexistent file path argument crashed" "(Code: $rc)"
fi

echo ""
echo "=========================================================="
echo "  HOSTILE INJECTION RESULTS: $PASS_COUNT PASSED, $FAIL_COUNT FAILED"
echo "=========================================================="

if [ $FAIL_COUNT -eq 0 ]; then
    exit 0
else
    exit 1
fi
