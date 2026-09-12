const std = @import("std");
pub const panic = std.debug.no_panic;
extern fn object_crc32c(bytes: [*]const u8, length: usize) u32;
fn u32le(b: []const u8, o: usize) u32 {
    return std.mem.readInt(u32, b[o..][0..4], .little);
}
fn u64le(b: []const u8, o: usize) u64 {
    return std.mem.readInt(u64, b[o..][0..8], .little);
}
fn zero(b: []const u8) bool {
    for (b) |v| {
        if (v != 0) return false;
    }
    return true;
}
pub export fn provenance_validate(raw: ?[*]const u8, length: usize) c_int {
    const pointer = raw orelse return 0;
    if (length != 24 + 32 * 280) return 0;
    const bytes = pointer[0..length];
    if (u64le(bytes, 0) != 0x4A414E4950525631 or u32le(bytes, 8) != 1 or
        u32le(bytes, 12) != 0 or u32le(bytes, 20) != 0 or
        object_crc32c(bytes[24..].ptr, length - 24) != u32le(bytes, 16)) return 0;
    for (0..32) |i| {
        const b = bytes[24 + i * 280 ..][0..280];
        if (zero(b[0..16])) {
            if (!zero(b)) return 0;
            continue;
        }
        const count = u32le(b, 16);
        const next = u32le(b, 20);
        if (count == 0 or count > 8 or next >= 8 or (count < 8 and next != count)) return 0;
        for (0..i) |j| {
            if (std.mem.eql(u8, b[0..16], bytes[24 + j * 280 ..][0..16])) return 0;
        }
        for (0..8) |j| {
            const event = b[24 + j * 32 ..][0..32];
            if (j >= count) {
                if (!zero(event)) return 0;
                continue;
            }
            if (zero(event[0..16]) or u32le(event, 24) < 1 or u32le(event, 24) > 6) return 0;
        }
    }
    return 1;
}
