const std = @import("std");
pub const panic = std.debug.no_panic;
fn word(b: []const u8, i: usize) u32 {
    return std.mem.readInt(u32, b[i..][0..4], .little);
}
pub export fn block_descriptors_validate(raw: ?[*]const u8, length: usize, operation: u32) c_int {
    const pointer = raw orelse return 0;
    if (operation != 0 and operation != 1 and operation != 4) return 0;
    if (length != (if (operation == 4) @as(usize, 24) else 36)) return 0;
    const bytes = pointer[0..length];
    if (word(bytes, 0) != 0 or word(bytes, 4) != 16 or word(bytes, 8) != 0) return 0;
    const last = length - 12;
    if (word(bytes, last) != 16 or word(bytes, last + 4) != 1 or word(bytes, last + 8) != 1) return 0;
    if (operation != 4 and (word(bytes, 12) != 512 or word(bytes, 16) != 512 or
        word(bytes, 20) != (if (operation == 0) @as(u32, 1) else 0))) return 0;
    return 1;
}
