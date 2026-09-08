const std = @import("std");
pub const panic = std.debug.no_panic;
extern fn object_crc32c(bytes: [*]const u8, length: usize) u32;
fn u32le(b: []const u8, o: usize) u32 {
    return std.mem.readInt(u32, b[o..][0..4], .little);
}
fn zero(b: []const u8) bool {
    for (b) |v| {
        if (v != 0) return false;
    }
    return true;
}
pub export fn scheduler_trace_validate(raw: ?[*]const u8, length: usize) c_int {
    const pointer = raw orelse return 0;
    if (length != 24 + 64 * 40) return 0;
    const bytes = pointer[0..length];
    if (std.mem.readInt(u64, bytes[0..8], .little) != 0x4A414E4954524331 or u32le(bytes, 20) != 0) return 0;
    const count = u32le(bytes, 8);
    const next = u32le(bytes, 12);
    if (count > 64 or next >= 64 or (count < 64 and next != count) or
        object_crc32c(bytes[24..].ptr, length - 24) != u32le(bytes, 16)) return 0;
    for (0..64) |i| {
        const event = bytes[24 + i * 40 ..][0..40];
        if (i >= count) {
            if (!zero(event)) return 0;
            continue;
        }
        if (zero(event[0..16]) or u32le(event, 32) < 1 or u32le(event, 32) > 5 or u32le(event, 36) != 0) return 0;
    }
    return 1;
}
