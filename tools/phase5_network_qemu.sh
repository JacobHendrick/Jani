#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
WORK="$(mktemp -d)"
PIDS=
LOG_PREFIX="${PHASE5_LOG_PREFIX:-$ROOT/build/phase5}"
CPU="${PHASE5_CPU:-max,-apic}"
SOCKET="$WORK/network.sock"
stop() {
    for pid in $PIDS; do kill -9 "$pid" 2>/dev/null || true; done
    for pid in $PIDS; do wait "$pid" 2>/dev/null || true; done
    PIDS=
    rm -f "$SOCKET"
}
cleanup() { stop; rm -rf "$WORK"; }
trap cleanup EXIT HUP INT TERM
umask 077
"$ROOT/build/net_provision" "$WORK/a.bin" "$WORK/b.bin"
for node in a b; do
    mkdir -p "$WORK/$node"
    cp -R "$ROOT/build/iso_root/." "$WORK/$node/"
    cp "$ROOT/build/phase5-jani.elf" "$WORK/$node/boot/jani.elf"
    cp "$WORK/$node.bin" "$WORK/$node/boot/net.bin"
    printf '\n    module_path: boot():/boot/net.bin\n' >> "$WORK/$node/boot/limine.conf"
    xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin -no-emul-boot \
        -boot-load-size 4 -boot-info-table --efi-boot boot/limine/limine-uefi-cd.bin \
        -efi-boot-part --efi-boot-image --protective-msdos-label "$WORK/$node" \
        -o "$WORK/$node.iso" >"$WORK/$node-iso.log" 2>&1
    "$ROOT/third_party/limine/limine" bios-install "$WORK/$node.iso" >>"$WORK/$node-iso.log" 2>&1
    truncate -s 64M "$WORK/$node.img"
done
boot_node() {
    node="$1"; server="$2"; mac="$3"
    : > "$LOG_PREFIX-node-$node.log"
    qemu-system-x86_64 -cpu "$CPU" -m 256 -cdrom "$WORK/$node.iso" \
        -serial "file:$LOG_PREFIX-node-$node.log" -display none -no-reboot \
        -drive "file=$WORK/$node.img,if=none,id=d0,format=raw,cache=writeback" \
        -device virtio-blk-pci,drive=d0 \
        -netdev "stream,id=n0,server=$server,addr.type=unix,addr.path=$SOCKET" \
        -device virtio-net-pci,netdev=n0,disable-legacy=on,mac="$mac" \
        >"$WORK/$node-qemu.log" 2>&1 &
    PIDS="$PIDS $!"
}
boot_pair() {
    boot_node a on 52:54:00:00:00:01
    remaining=20
    while [ ! -S "$SOCKET" ] && [ "$remaining" -gt 0 ]; do sleep 1; remaining=$((remaining - 1)); done
    test -S "$SOCKET"
    boot_node b off 52:54:00:00:00:02
}
wait_pair() {
    remaining="${1:-180}"
    while [ "$remaining" -gt 0 ]; do
        if grep -q 'PHASE5 FAIL\|PANIC\|ERROR:\|SYSCALL FAIL' \
            "$LOG_PREFIX-node-a.log" "$LOG_PREFIX-node-b.log" 2>/dev/null; then break; fi
        if grep -q '^PHASE5 PASS' "$LOG_PREFIX-node-a.log" &&
           grep -q '^PHASE5 PASS' "$LOG_PREFIX-node-b.log"; then
            a="$(sed -n 's/^phase5: converged digest //p' "$LOG_PREFIX-node-a.log" | tr -d '\r' | tail -1)"
            b="$(sed -n 's/^phase5: converged digest //p' "$LOG_PREFIX-node-b.log" | tr -d '\r' | tail -1)"
            test "${#a}" -eq 64 && test "$a" = "$b"
            for node in a b; do
                grep -q 'virtio-net: Zig protocol component ready' "$LOG_PREFIX-node-$node.log"
                grep -q 'pinned peer authenticated' "$LOG_PREFIX-node-$node.log"
                grep 'phase5:\|counter:\|counter mailbox:\|PHASE5' "$LOG_PREFIX-node-$node.log"
            done
            return 0
        fi
        for pid in $PIDS; do kill -0 "$pid" 2>/dev/null || break 2; done
        sleep 1; remaining=$((remaining - 1))
    done
    echo "FAIL: Phase 5 milestone"
    tail -n 40 "$LOG_PREFIX-node-a.log" "$LOG_PREFIX-node-b.log"
    cat "$WORK/a-qemu.log" "$WORK/b-qemu.log"
    return 1
}
values() { tr -d '\r' < "$1" | sed -n 's/^counter: //p'; }
boot_pair
if [ "${PHASE5_EXPECT_ENTROPY_FAILURE:-0}" -eq 1 ]; then
    remaining=90
    while [ "$remaining" -gt 0 ]; do
        if grep -q 'PHASE5 FAIL: identity or fresh entropy unavailable' "$LOG_PREFIX-node-a.log" &&
           grep -q 'PHASE5 FAIL: identity or fresh entropy unavailable' "$LOG_PREFIX-node-b.log"; then
            if grep -q 'pinned peer authenticated\|^PHASE5 PASS' "$LOG_PREFIX-node-a.log" "$LOG_PREFIX-node-b.log"; then exit 1; fi
            echo "PASS: no-RDRAND peers fail closed before session establishment"; exit 0
        fi
        sleep 1; remaining=$((remaining-1))
    done
    echo "FAIL: entropy rejection missing"; exit 1
fi
wait_pair "${1:-180}"
test "$(values "$LOG_PREFIX-node-a.log")" = "$(printf '1\n2\n3')"
test "$(values "$LOG_PREFIX-node-b.log")" = "$(printf '4\n5\n6')"
for node in a b; do
    grep -q 'edited while transport offline' "$LOG_PREFIX-node-$node.log"
    cp "$LOG_PREFIX-node-$node.log" "$ROOT/build/phase5-first-$node.log"
done
test "$(grep -c 'counter mailbox: migrated ping' "$LOG_PREFIX-node-b.log")" -eq 1
stop
echo "phase5: rebooting both nodes against the same disks"
boot_pair
wait_pair "${1:-180}"
grep -q 'durable source fence restored' "$LOG_PREFIX-node-a.log"
grep -q 'imported component resumed from disk' "$LOG_PREFIX-node-b.log"
test -z "$(values "$LOG_PREFIX-node-a.log")"
test "$(values "$LOG_PREFIX-node-b.log")" = "$(printf '7\n8\n9')"
if grep -q 'component: initialized\|counter mailbox:' \
    "$LOG_PREFIX-node-a.log" "$LOG_PREFIX-node-b.log"; then
    echo "FAIL: initializer or forwarded mailbox repeated after restart"; exit 1
fi
for node in a b; do
    first_node="$(tr -d '\r' < "$ROOT/build/phase5-first-$node.log" | sed -n 's/^phase5: node //p')"
    next_node="$(tr -d '\r' < "$LOG_PREFIX-node-$node.log" | sed -n 's/^phase5: node //p')"
    first_sid="$(tr -d '\r' < "$ROOT/build/phase5-first-$node.log" | sed -n 's/^phase5: session //p')"
    next_sid="$(tr -d '\r' < "$LOG_PREFIX-node-$node.log" | sed -n 's/^phase5: session //p')"
    test "${#first_node}" -eq 64 && test "$first_node" = "$next_node"
    test "${#first_sid}" -eq 64 && test "${#next_sid}" -eq 64 && test "$first_sid" != "$next_sid"
done
test "$(sed -n 's/^phase5: session //p' "$LOG_PREFIX-node-a.log")" = "$(sed -n 's/^phase5: session //p' "$LOG_PREFIX-node-b.log")"
echo "PASS: authenticated convergence, counter migration, forwarding, and durable ownership after reboot"
