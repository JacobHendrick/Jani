#!/bin/sh
#
# The blueprint's milestone demo, as a gate.
#
# A counter component ticks over serial. QEMU is powered off entirely -- not
# suspended, not signalled, killed -- and booted again against the same disk.
# The counter must continue from where it stopped.
#
# The assertion allows two values, and the reason matters. The component
# prints inside its handler; the commit happens after the handler returns. A
# kill landing in that window loses the printed tick, so the next boot repeats
# it. A kill after the commit keeps it, so the next boot advances. Both are
# correct. Anything else is a real bug: lower means a committed tick was lost,
# higher means a tick was double-counted or jani_init ran on resume.
#
# Usage: tools/wow_demo_qemu.sh [cycles] [seconds_per_cycle]

set -u

CYCLES="${1:-3}"
RUN_SECONDS="${2:-40}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
DISK="$WORK/wow-disk.img"
LOG="$WORK/serial.log"
ISO="$ROOT/build/jani.iso"

cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

if [ ! -f "$ISO" ]; then
    echo "error: $ISO not found; run 'make iso' first" >&2
    exit 1
fi

truncate -s 64M "$DISK"

boot_once() {
    : > "$LOG"

    qemu-system-x86_64 -cpu qemu64,-apic \
        -cdrom "$ISO" \
        -serial "file:$LOG" \
        -display none -no-reboot \
        -drive "file=$DISK,if=none,id=d0,format=raw,cache=writeback" \
        -device virtio-blk-pci,drive=d0 >/dev/null 2>&1 &
    local pid=$!

    sleep "$RUN_SECONDS"

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
    exit 1
}

counter_values() {
    tr -d '\r' < "$LOG" 2>/dev/null | grep '^counter: ' | sed 's/^counter: //'
}

echo "wow demo: $CYCLES power cycles, ${RUN_SECONDS}s each"

cycle=1
last_seen=0

while [ "$cycle" -le "$CYCLES" ]; do
    boot_once

    first="$(counter_values | head -1)"
    last="$(counter_values | tail -1)"

    if [ -z "$first" ]; then
        fail "$cycle" "no counter output at all"
    fi

    if [ "$cycle" -eq 1 ]; then
        if [ "$first" -ne 1 ]; then
            fail "$cycle" "first boot started at $first, expected 1"
        fi
        if ! grep -q 'component: initialized' "$LOG"; then
            fail "$cycle" "first boot did not report installation"
        fi
    else
        if ! grep -q 'component: resumed' "$LOG"; then
            fail "$cycle" "boot $cycle did not resume; it reinstalled"
        fi
        if grep -q 'component: initialized' "$LOG"; then
            fail "$cycle" "jani_init ran on resume"
        fi

        expected_low="$last_seen"
        expected_high="$((last_seen + 1))"

        if [ "$first" -lt "$expected_low" ]; then
            fail "$cycle" \
                "resumed at $first, below last committed $expected_low"
        fi
        if [ "$first" -gt "$expected_high" ]; then
            fail "$cycle" \
                "resumed at $first, above $expected_high (tick double-counted)"
        fi
    fi

    if grep -q 'ERROR:' "$LOG"; then
        fail "$cycle" "kernel reported an error"
    fi

    echo "  cycle $cycle  counter $first..$last"
    last_seen="$last"
    cycle=$((cycle + 1))
done

echo
echo "PASS: counter survived $CYCLES power cycles, ending at $last_seen"
echo "      (resumed exactly, with jani_init run only on the first boot)"
