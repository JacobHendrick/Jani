const std = @import("std");
pub const panic = std.debug.no_panic;

const View = extern struct {
    kind: u32,
    reserved: u32,
    sequence: u64,
    payload_length: usize,
    signed_length: usize,
};

export fn net_wire_validate(raw: ?[*]const u8, length: usize,
    out: ?*View) callconv(.c) c_int {
    const input = raw orelse return 0;
    const output = out orelse return 0;
    if (length < 8 or length > 1160) return 0;
    const bytes = input[0..length];
    if (bytes[4] != 1 or bytes[6] != 0 or bytes[7] != 0) return 0;
    if (std.mem.eql(u8, bytes[0..4], "JN5H")) {
        if (length != 136 or bytes[5] != 0) return 0;
        output.* = .{ .kind = 0, .reserved = 0, .sequence = 0,
            .payload_length = 0, .signed_length = 72 };
        return 1;
    }
    if (!std.mem.eql(u8, bytes[0..4], "JN5D") or length < 136) return 0;
    if (bytes[5] < 1 or bytes[5] > 5) return 0;
    const sequence = std.mem.readInt(u64, bytes[40..48], .little);
    const payload_length = std.mem.readInt(u16, bytes[48..50], .little);
    if (sequence == 0 or payload_length > 1024 or length != 136 + @as(usize, payload_length)) return 0;
    for (bytes[50..56]) |byte| if (byte != 0) return 0;
    output.* = .{ .kind = bytes[5], .reserved = 0, .sequence = sequence,
        .payload_length = payload_length, .signed_length = length - 64 };
    return 1;
}

export fn net_seed_decode(raw: ?[*]const u8, length: usize,
    out: ?*[32]u8) callconv(.c) c_int {
    const input = raw orelse return 0;
    const output = out orelse return 0;
    if (length != 40) return 0;
    const bytes = input[0..40];
    if (!std.mem.eql(u8, bytes[0..4], "JK5S") or bytes[4] != 1) return 0;
    for (bytes[5..8]) |byte| if (byte != 0) return 0;
    const seed: [32]u8 = bytes[8..40].*;
    output.* = seed;
    return 1;
}
