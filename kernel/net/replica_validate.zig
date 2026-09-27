const std = @import("std");
pub const panic = std.debug.no_panic;
const Id = extern struct { high: u64, low: u64 };
const Scope = extern struct { id: Id, epoch: u64, kind: u32, rights: u32, owned: u32, reserved: u32 };
const Catalog = extern struct { count: u32, reserved: u32, scopes: [8]Scope };
const Message = extern struct { id: Id, epoch: u64, kind: u32, rights: u32, vector: [2]u64, digest: [32]u8 };
fn scope_valid(s: Scope) bool {
    return s.id.high >= (1 << 63) and s.id.low != 0 and s.epoch != 0 and
        s.kind >= 1 and s.kind <= 2 and s.rights <= 3 and s.owned <= 1 and s.reserved == 0;
}
fn catalog_valid(c: Catalog) bool {
    if (c.count > 8 or c.reserved != 0) return false;
    for (c.scopes[0..c.count], 0..) |s, i| {
        if (!scope_valid(s)) return false;
        for (c.scopes[0..i]) |prior| {
            if (s.id.high == prior.id.high and s.id.low == prior.id.low) return false;
        }
    }
    for (c.scopes[c.count..]) |s| {
        if (s.id.high != 0 or s.id.low != 0 or s.epoch != 0 or s.kind != 0 or
            s.rights != 0 or s.owned != 0 or s.reserved != 0) return false;
    }
    return true;
}
export fn replica_catalog_encode(raw: ?[*]u8, capacity: usize,
    local: ?[*]const u8, peer: ?[*]const u8, input: ?*const Catalog) callconv(.c) usize {
    const out = raw orelse return 0;
    const l = local orelse return 0;
    const p = peer orelse return 0;
    const c = (input orelse return 0).*;
    if (capacity < 400 or !catalog_valid(c) or std.mem.eql(u8, l[0..32], p[0..32])) return 0;
    var bytes = [_]u8{0} ** 400;
    @memcpy(bytes[0..4], "JC5A");
    bytes[4] = 1;
    @memcpy(bytes[8..40], l[0..32]);
    @memcpy(bytes[40..72], p[0..32]);
    std.mem.writeInt(u32, bytes[72..76], c.count, .little);
    for (c.scopes, 0..) |s, i| {
        const at = 80 + 40*i;
        std.mem.writeInt(u64, bytes[at..][0..8], s.id.high, .little);
        std.mem.writeInt(u64, bytes[at+8..][0..8], s.id.low, .little);
        std.mem.writeInt(u64, bytes[at+16..][0..8], s.epoch, .little);
        std.mem.writeInt(u32, bytes[at+24..][0..4], s.kind, .little);
        std.mem.writeInt(u32, bytes[at+28..][0..4], s.rights, .little);
        std.mem.writeInt(u32, bytes[at+32..][0..4], s.owned, .little);
    }
    @memcpy(out[0..400], &bytes);
    return 400;
}
export fn replica_catalog_decode(raw: ?[*]const u8, length: usize,
    local: ?[*]const u8, peer: ?[*]const u8, output: ?*Catalog) callconv(.c) c_int {
    const input = raw orelse return 0;
    const l = local orelse return 0;
    const p = peer orelse return 0;
    const out = output orelse return 0;
    if (length != 400) return 0;
    const b = input[0..400];
    if (!std.mem.eql(u8, b[0..4], "JC5A") or b[4] != 1 or
        !std.mem.eql(u8, b[8..40], l[0..32]) or !std.mem.eql(u8, b[40..72], p[0..32])) return 0;
    for (b[5..8]) |v| if (v != 0) return 0;
    var c = std.mem.zeroes(Catalog);
    c.count = std.mem.readInt(u32, b[72..76], .little);
    c.reserved = std.mem.readInt(u32, b[76..80], .little);
    for (&c.scopes, 0..) |*s, i| {
        const at = 80 + 40*i;
        s.* = .{ .id = .{ .high = std.mem.readInt(u64, b[at..][0..8], .little),
            .low = std.mem.readInt(u64, b[at+8..][0..8], .little) },
            .epoch = std.mem.readInt(u64, b[at+16..][0..8], .little),
            .kind = std.mem.readInt(u32, b[at+24..][0..4], .little),
            .rights = std.mem.readInt(u32, b[at+28..][0..4], .little),
            .owned = std.mem.readInt(u32, b[at+32..][0..4], .little),
            .reserved = std.mem.readInt(u32, b[at+36..][0..4], .little) };
    }
    if (!catalog_valid(c)) return 0;
    out.* = c;
    return 1;
}
export fn replica_message_decode(raw: ?[*]const u8, length: usize,
    packet_kind: u32, output: ?*Message) callconv(.c) c_int {
    const input = raw orelse return 0;
    const out = output orelse return 0;
    const expected: usize = switch (packet_kind) { 1 => 80, 2 => 712, 3 => 32, else => return 0 };
    if (length != expected) return 0;
    const b = input[0..length];
    if (!std.mem.eql(u8, b[0..4], "JR5O") or b[4] != 1 or b[5] != packet_kind or
        b[6] < 1 or b[6] > 2 or b[7] > 3 or (packet_kind != 3 and b[7] != 0)) return 0;
    var m = std.mem.zeroes(Message);
    m.id.high = std.mem.readInt(u64, b[8..16], .little);
    m.id.low = std.mem.readInt(u64, b[16..24], .little);
    m.epoch = std.mem.readInt(u64, b[24..32], .little);
    m.kind = b[6]; m.rights = b[7];
    if (m.id.high < (1 << 63) or m.id.low == 0 or m.epoch == 0) return 0;
    if (packet_kind == 1) {
        m.vector[0] = std.mem.readInt(u64, b[32..40], .little);
        m.vector[1] = std.mem.readInt(u64, b[40..48], .little);
        @memcpy(&m.digest, b[48..80]);
    }
    out.* = m;
    return 1;
}
