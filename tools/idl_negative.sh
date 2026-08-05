#!/usr/bin/env bash
set -u

IDLC="$1"
BUILD_DIR="$2"
GENERATED_DIR="$3"

WORK="${BUILD_DIR}/idl-negative"
rm -rf "${WORK}"
mkdir -p "${WORK}"

rejected=0
accepted=0

report() {
    if [ "$2" = "rejected" ]; then
        rejected=$((rejected + 1))
        echo "  ok       $1 was rejected"
    else
        accepted=$((accepted + 1))
        echo "  ACCEPTED $1 was NOT rejected"
    fi
}

echo "idl-negative: seeding three defects"

sed 's|delay_ticks: u64|delay_ticks: i64|' idl/syscalls.idl \
    > "${WORK}/retyped.idl"
"${IDLC}" --emit=table --out="${WORK}/retyped_table.h" \
    "${WORK}/retyped.idl" >/dev/null 2>&1
"${IDLC}" --emit=zig --out="${WORK}/retyped.zig" \
    "${WORK}/retyped.idl" >/dev/null 2>&1

if ! diff -q "${GENERATED_DIR}/syscall_table.h" "${WORK}/retyped_table.h" \
    >/dev/null 2>&1; then
    echo "  UNEXPECTED the WAMR table changed for a signedness-only edit"
    accepted=$((accepted + 1))
elif diff -q sdk/zig/jani.zig "${WORK}/retyped.zig" >/dev/null 2>&1; then
    report "a retyped argument" "accepted"
else
    report "a retyped argument" "rejected"
fi

sed 's|state_id:             object-ref @ 48;|state_id:             object-ref @ 40;|' \
    idl/records.idl > "${WORK}/moved.idl"
"${IDLC}" --emit=conform --out="${WORK}/moved_conform.h" \
    "${WORK}/moved.idl" >/dev/null 2>&1
cat > "${WORK}/moved_check.c" <<'EOF'
#include "kernel/wasm/component.h"
#include "kernel/wasm/instance_state.h"
#include "moved_conform.h"
int main(void) { return 0; }
EOF
if clang -I. -I"${WORK}" -c "${WORK}/moved_check.c" -o "${WORK}/moved_check.o" \
    >/dev/null 2>&1; then
    report "a moved record field" "accepted"
else
    report "a moved record field" "rejected"
fi

grep -v '^syscall self()' idl/syscalls.idl > "${WORK}/deleted.idl"
"${IDLC}" --emit=table --out="${WORK}/deleted_table.h" \
    "${WORK}/deleted.idl" >/dev/null 2>&1
if grep -q 'jani_self_impl' "${WORK}/deleted_table.h"; then
    report "a deleted syscall" "accepted"
else
    report "a deleted syscall" "rejected"
fi

echo "idl-negative: ${rejected}/3 defects rejected"

if [ "${accepted}" -ne 0 ]; then
    echo "idl-negative: FAILED - ${accepted} defect(s) slipped through"
    exit 1
fi

exit 0
