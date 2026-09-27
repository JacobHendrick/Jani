const std = @import("std");
pub const panic = std.debug.no_panic;
const Config = extern struct {
    role: u32, reserved: u32,
    seed: [32]u8, peer_key: [32]u8, local_key: [32]u8,
};
export fn net_config_decode(raw: ?[*]const u8, length: usize,
    out: ?*Config) callconv(.c) c_int {
    const input = raw orelse return 0;
    const output = out orelse return 0;
    if (length != 104) return 0;
    const bytes = input[0..104];
    if (!std.mem.eql(u8, bytes[0..4], "JN5C") or bytes[4] != 1 or
        bytes[5] > 1 or bytes[6] != 0 or bytes[7] != 0) return 0;
    const value: Config = .{ .role = bytes[5], .reserved = 0,
        .seed = bytes[8..40].*, .peer_key = bytes[40..72].*,
        .local_key = bytes[72..104].* };
    if (std.mem.eql(u8, &value.local_key, &value.peer_key)) return 0;
    output.* = value; return 1;
}
