const jani = @import("jani");
const std = @import("std");
pub const State = extern struct { count: u64, slot: i32, reserved: u32 };

// The module build reserves this range outside its data and stack segments.
fn state() *volatile State {
    return @ptrFromInt(49152);
}
pub fn init() void {
    state().* = .{ .count = 0, .slot = -1, .reserved = 0 };
}
pub fn message(version: u64) void {
    var payload: [8]u8 = undefined;
    var slot: i32 = -1;
    if (jani.message_recv(&payload, &slot) != payload.len or std.mem.readInt(u64, &payload, .little) != 0) {
        _ = jani.log("PHASE4 FAIL: message schema\n");
        return;
    }
    if (slot >= 0) state().slot = slot;
    var bytes = [_]u8{42};
    if (jani.object_write(state().slot, 0, &bytes) != -2) {
        _ = jani.log("PHASE4 FAIL: write permission\n");
        return;
    }
    _ = jani.log("phase4: unauthorized write denied\n");
    if (jani.object_read(state().slot, 0, &bytes) != 1 or bytes[0] != 7) {
        _ = jani.log("PHASE4 FAIL: read permission\n");
        return;
    }
    _ = jani.log("phase4: allowed read\n");
    if (jani.driver_restart() != -2 or jani.dma_read(0, 0, &bytes) != -2) {
        _ = jani.log("PHASE4 FAIL: device capability\n");
        return;
    }
    _ = jani.log("phase4: direct device access denied\n");
    var stats: [32]u8 = undefined;
    var trace: [40]u8 = undefined;
    if (jani.stats(&stats) != stats.len or jani.trace(0, &trace) != trace.len) {
        _ = jani.log("PHASE4 FAIL: introspection\n");
        return;
    }
    state().count += version;
    _ = jani.log(if (version == 2) "phase4: service v2 handler\n" else "phase4: service v1 handler\n");
}
