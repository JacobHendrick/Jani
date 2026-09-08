const std = @import("std");
pub const panic = std.debug.no_panic;
fn u32le(b: []const u8, o: usize) u32 {
    return std.mem.readInt(u32, b[o..][0..4], .little);
}
pub export fn replay_log_validate(raw: ?[*]const u8, length: usize) c_int {
    const pointer = raw orelse return 0;
    if (length == 0 or length > 65536) return 0;
    const bytes = pointer[0..length];
    var offset: usize = 0;
    var sequence: u64 = 0;
    while (offset < length) {
        if (length - offset < 96) return 0;
        const record = bytes[offset..][0..96];
        if (u32le(record, 0) > 32 or u32le(record, 4) != 0 or
            std.mem.readInt(u64, record[8..16], .little) != sequence) return 0;
        if (sequence == 0 and u32le(record, 0) != 0) return 0;
        const input = u32le(record, 88);
        const output = u32le(record, 92);
        if (input > 8192 or output > 8192) return 0;
        if (u32le(record, 0) == 0) {
            if (input != 0 or output != 0 or std.mem.readInt(u64, record[24..32], .little) > 1) return 0;
            for (record[32..88]) |byte| if (byte != 0) return 0;
        }
        offset += 96;
        if (input + output > length - offset) return 0;
        offset += input + output;
        sequence += 1;
    }
    return 1;
}
