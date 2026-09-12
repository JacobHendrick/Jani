#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
WORK="$(mktemp -d)"
LOG="$ROOT/build/phase4-demo.log"
PID=
cleanup() {
    if [ -n "$PID" ]; then
        kill "$PID" 2>/dev/null || true
        wait "$PID" 2>/dev/null || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT HUP INT TERM
truncate -s 64M "$WORK/disk.img"
: > "$LOG"
qemu-system-x86_64 -cpu qemu64,-apic -cdrom "$ROOT/build/jani.iso" \
    -serial "file:$LOG" -display none -no-reboot \
    -drive "file=$WORK/disk.img,if=none,id=d0,format=raw,cache=writeback" \
    -device virtio-blk-pci,drive=d0 >"$WORK/qemu.log" 2>&1 &
PID=$!
remaining="${1:-180}"
while [ "$remaining" -gt 0 ] && kill -0 "$PID" 2>/dev/null; do
    if grep -q 'PHASE4 FAIL\|PANIC' "$LOG" 2>/dev/null; then break; fi
    if grep -q 'PHASE4 PASS' "$LOG" 2>/dev/null; then
        if grep 'ERROR:' "$LOG" | grep -v "call 'jani_kill' failed: Exception: unreachable"; then exit 1; fi
        for marker in 'unauthorized write denied' 'allowed read' \
            'replay memory identical' 'persisted recording replayed' 'direct device access denied' \
            'hot-swap preserved pending mailbox' \
            'service v2' 'service rollback succeeded' 'remote descendant revoked' \
            'storage driver restarted; disk bytes unchanged'; do
            if ! grep -q "$marker" "$LOG"; then
                echo "FAIL: missing milestone: $marker"
                exit 1
            fi
        done
        grep 'phase4:\|PHASE4' "$LOG"
        exit 0
    fi
    sleep 1
    remaining=$((remaining - 1))
done
echo "FAIL: Phase 4 milestone did not complete; serial log: $LOG"
tail -40 "$LOG"
exit 1
