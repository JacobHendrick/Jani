const std = @import("std");
pub const panic = std.debug.no_panic;
extern fn object_crc32c(bytes: [*]const u8, length: usize) u32;
fn u32le(b: []const u8, o: usize) u32 {
    return std.mem.readInt(u32, b[o..][0..4], .little);
}
fn u64le(b: []const u8, o: usize) u64 {
    return std.mem.readInt(u64, b[o..][0..8], .little);
}
pub export fn service_binding_validate(raw: ?[*]const u8, length: usize) c_int {
    const pointer = raw orelse return 0;
    if (length != 112) return 0;
    const bytes = pointer[0..length];
    if (u64le(bytes, 0) != 0x4A414E4953564331 or u32le(bytes, 8) != 1 or
        u32le(bytes, 12) != 0 or u32le(bytes, 108) != 0 or u32le(bytes, 100) == 0 or
        u64le(bytes, 16) != 2 or u64le(bytes, 24) == 0 or
        (u64le(bytes, 32) == 0 and u64le(bytes, 40) == 0) or u64le(bytes, 64) == 0 or u64le(bytes, 72) == 0 or
        u64le(bytes, 80) == 0 or u64le(bytes, 80) > 0xffffffff or object_crc32c(pointer, 104) != u32le(bytes, 104)) return 0;
    if (u64le(bytes, 80) > 1 and ((u64le(bytes, 48) == 0 and u64le(bytes, 56) == 0) or u64le(bytes, 88) == 0)) return 0;
    return 1;
}
