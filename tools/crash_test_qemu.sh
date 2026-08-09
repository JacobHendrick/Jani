#!/usr/bin/env bash
#
# Phase 2 milestone gate: kill QEMU mid-write, reboot, verify the store
# recovers to a consistent state. Blueprint PHASE 2, MILESTONE DEMO.
#
# Each cycle boots the kernel against a persistent disk image and kills the
# process at a random moment during the write workload. The NEXT boot is the
# check: it mounts the store and verifies every object is readable, that its
# header version matches the table, and that its payload matches the pattern
# implied by that version. Cross-object clobbering and torn writes both fail
# that test.
#
# A store that cannot be mounted at all is also a failure. The only legal
# outcomes after a crash are "mounted and fully consistent" or, on the very
# first cycle, "no valid store yet".
#
# HONEST SCOPE -- read this before trusting a PASS.
#
# Killing the QEMU process is NOT a power cut. Writes QEMU has already issued
# live on in the host page cache and reach the file regardless, so nothing
# acknowledged is ever lost here. This was measured, not assumed: running with
# CACHE_MODE=unsafe, where QEMU discards every flush request outright, still
# passes every cycle. A driver whose flush did nothing at all would sail
# through this test.
#
# So this covers: crash-consistency of the commit SEQUENCE (the guest stops
# mid-protocol and must recover), unmountable stores, torn objects, and
# cross-object clobbering. It does NOT cover whether flush is a real
# durability barrier. That needs actual power loss on real hardware, and it
# remains the single most important unverified assumption in the store --
# every crash-safety property in WalCommit.tla rests on it.
#
# Usage: tools/crash_test_qemu.sh [cycles] [disk_mib]
#        CACHE_MODE=unsafe tools/crash_test_qemu.sh   (flush-ignoring control)

set -u

CYCLES="${1:-25}"
DISK_MIB="${2:-8}"
CACHE_MODE="${CACHE_MODE:-writeback}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
DISK="$WORK/crash-disk.img"
LOG="$WORK/serial.log"
ISO="$ROOT/build/jani.iso"

cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

if [ ! -f "$ISO" ]; then
    echo "error: $ISO not found; run 'make iso' first" >&2
    exit 1
fi

truncate -s "${DISK_MIB}M" "$DISK"

VERDICT_PATTERN='RECOVERY OK|RECOVERY FAILED|workload complete|ERROR:'

# The kernel never halts -- after the demo it falls through to the timer tick
# loop forever. So every boot here is bounded: either killed at a chosen
# moment (that is the crash), or run until the serial log shows a verdict and
# then stopped. There is no path that waits on QEMU to exit by itself.
boot_once() {
    local kill_after="$1"
    local waited=0
    : > "$LOG"

    qemu-system-x86_64 -cpu qemu64,-apic \
        -cdrom "$ISO" \
        -serial "file:$LOG" \
        -display none -no-reboot \
        -drive "file=$DISK,if=none,id=d0,format=raw,cache=$CACHE_MODE" \
        -device virtio-blk-pci,drive=d0 >/dev/null 2>&1 &
    local pid=$!

    if [ -n "$kill_after" ]; then
        while [ "$waited" -lt 600 ]; do
            if grep -q "workload round" "$LOG" 2>/dev/null; then
                break
            fi
            if grep -q "RECOVERY FAILED\|ERROR:" "$LOG" 2>/dev/null; then
                break
            fi
            sleep 0.1
            waited=$((waited + 1))
        done

        if grep -q "workload round" "$LOG" 2>/dev/null; then
            sleep "$kill_after"
        fi
    else
        while [ "$waited" -lt 600 ]; do
            if grep -qE "$VERDICT_PATTERN" "$LOG" 2>/dev/null; then
                break
            fi
            sleep 0.1
            waited=$((waited + 1))
        done
    fi

    kill -9 "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    return 0
}

fail() {
    echo
    echo "=============================================="
    echo "FAILURE on cycle $1: $2"
    echo "=============================================="
    sed -n '/virtio-blk ready/,$p' "$LOG" | head -40
    cp "$DISK" "$ROOT/crash-failure-disk.img"
    echo "disk image preserved at $ROOT/crash-failure-disk.img"
    exit 1
}

echo "crash test: $CYCLES cycles against a ${DISK_MIB} MiB virtio-blk disk (cache=$CACHE_MODE)"

writes_seen=0

for cycle in $(seq 1 "$CYCLES"); do
    # Wait for the first workload marker, then choose a random cut inside it.
    delay="$(awk -v seed="$cycle$$" 'BEGIN{srand(seed);printf "%.2f", 0.05+rand()*0.95}')"

    boot_once "$delay"

    if grep -q "RECOVERY FAILED\|ERROR:" "$LOG"; then
        fail "$cycle" "the boot before this one left an inconsistent store"
    fi

    rounds="$(grep -c "workload round" "$LOG" || true)"
    if [ "$rounds" -eq 0 ]; then
        fail "$cycle" "the write workload never started"
    fi
    writes_seen=$((writes_seen + rounds))

    if grep -q "RECOVERY OK" "$LOG"; then
        state="recovered"
    elif grep -q "no valid store" "$LOG"; then
        state="formatted"
    elif grep -q "virtio-blk ready" "$LOG"; then
        state="killed during init"
    else
        state="killed before virtio came up"
    fi

    printf "  cycle %2d  cut+%-5ss   %-26s rounds=%s\n" \
        "$cycle" "$delay" "$state" "$rounds"
done

echo
echo "final verification boot (runs to a verdict, no kill)"
boot_once ""

if grep -q "RECOVERY FAILED\|ERROR:" "$LOG"; then
    fail "final" "store is inconsistent after the last crash"
fi

if ! grep -q "RECOVERY OK" "$LOG"; then
    fail "final" "store did not mount on a clean boot"
fi

echo
grep -E "RECOVERY OK|store mounted" "$LOG"
echo "PASS: store mounted and verified consistent after $CYCLES crash cycles"
echo "      (~$writes_seen workload rounds of write traffic interrupted)"
