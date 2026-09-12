const std = @import("std");
pub const panic = std.debug.no_panic;
extern fn object_crc32c(bytes: [*]const u8, length: usize) u32;
fn word(b: []const u8, i: usize) u32 {
    return std.mem.readInt(u32, b[i..][0..4], .little);
}
fn wide(b: []const u8, i: usize) u64 {
    return std.mem.readInt(u64, b[i..][0..8], .little);
}
pub export fn instance_state_bytes_validate(raw: ?[*]const u8, length: usize) c_int {
    const pointer = raw orelse return 0;
    if (length < 64) return 0;
    const b = pointer[0..length];
    if (wide(b, 0) != 0x4A414E495F535441 or word(b, 8) != 1 or word(b, 12) != 64 or
        word(b, 32) > 1 or word(b, 52) != 0 or wide(b, 56) != 0) return 0;
    const mailbox = word(b, 36);
    if (mailbox > 4096 or mailbox > length - 64 or wide(b, 40) != length - 64 - mailbox) return 0;
    if (component_mailbox_validate(pointer + 64, mailbox) == 0 or
        object_crc32c(pointer + 64, length - 64) != word(b, 48)) return 0;
    return 1;
}
pub export fn component_mailbox_validate(raw: ?[*]const u8, length: usize) c_int {
    const pointer = raw orelse return 0;
    if (length > 4096) return 0;
    const bytes = pointer[0..length];
    var offset: usize = 0;
    while (offset < length) {
        if (length - offset < 8) return 0;
        const size = std.mem.readInt(u32, bytes[offset..][0..4], .little);
        const slot = std.mem.readInt(u32, bytes[offset + 4 ..][0..4], .little);
        if (slot != 0xffffffff and slot >= 16) return 0;
        offset += 8;
        if (size > length - offset) return 0;
        offset += size;
    }
    return 1;
}
