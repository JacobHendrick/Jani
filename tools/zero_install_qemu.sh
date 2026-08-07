#!/bin/sh
# Prove that an installed component runs without its original Wasm file.

set -u

RUN_SECONDS="${1:-40}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
DISK="$WORK/zero-install-disk.img"
LOG="$WORK/serial.log"
INSTALL_ISO="$ROOT/build/jani.iso"
RESUME_ISO="$ROOT/build/jani-resume.iso"

cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

fail() {
    echo
    echo "=============================================="
    echo "ZERO-INSTALL FAILURE: $1"
    echo "=============================================="
    sed -n '/virtio-blk ready/,$p' "$LOG" | head -50
    exit 1
}

counter_values() {
    tr -d '\r' < "$LOG" 2>/dev/null | grep '^counter: ' | sed 's/^counter: //'
}

boot_once() {
    iso="$1"
    : > "$LOG"

    qemu-system-x86_64 -cpu qemu64,-apic \
        -cdrom "$iso" \
        -serial "file:$LOG" \
        -display none -no-reboot \
        -drive "file=$DISK,if=none,id=d0,format=raw,cache=writeback" \
        -device virtio-blk-pci,drive=d0 >/dev/null 2>&1 &
    pid=$!

    sleep "$RUN_SECONDS"
    kill -9 "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
}

truncate -s 64M "$DISK"

echo "zero-install: installing from the normal ISO"
boot_once "$INSTALL_ISO"

first_install="$(counter_values | head -1)"
last_install="$(counter_values | tail -1)"

if [ -z "$first_install" ]; then
    fail "the installation boot produced no counter output"
fi
if ! grep -q 'component: initialized' "$LOG"; then
    fail "the first boot did not install the component"
fi
if grep -q 'ERROR:' "$LOG"; then
    fail "the installation boot reported an error"
fi

echo "  installed, counter $first_install..$last_install"
echo "zero-install: booting the same disk from an ISO with no Wasm module"
boot_once "$RESUME_ISO"

first_resume="$(counter_values | head -1)"
last_resume="$(counter_values | tail -1)"

if [ -z "$first_resume" ]; then
    fail "the module-free boot produced no counter output"
fi
if ! grep -q 'component: resumed' "$LOG"; then
    fail "the module-free boot did not resume the installed component"
fi
if grep -q 'component: initialized' "$LOG"; then
    fail "the module-free boot ran jani_init again"
fi
if grep -q 'ERROR:' "$LOG"; then
    fail "the module-free boot reported an error"
fi
if [ "$first_resume" -lt "$last_install" ] ||
   [ "$first_resume" -gt "$((last_install + 1))" ]; then
    fail "counter resumed at $first_resume after ending at $last_install"
fi

echo "  resumed, counter $first_resume..$last_resume"
echo
echo "PASS: the component runs from the object store without counter.wasm"
