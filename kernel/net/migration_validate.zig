const std = @import("std");
pub const panic = std.debug.no_panic;
extern fn object_crc32c([*]const u8, usize) u32;
extern fn instance_state_bytes_validate([*]const u8, usize) c_int;
extern fn jani_wasm_module_validate([*]const u8, usize, *u32) c_int;
const Id = extern struct { high: u64, low: u64 };
const Record = extern struct {
    peer: [32]u8, digest: [32]u8, root: Id, module: Id, state: Id, captable: Id,
    generation: u64, target: Id, forward_sequence: u64,
};
const Receipt = extern struct {
    peer: [32]u8, digest: [32]u8, source: Id, generation: u64,
    target: Id, forward_sequence: u64,
};
const Bundle = extern struct {
    root: Id, generation: u64, module_length: u32, state_length: u32,
    captable_length: u32, reserved: u32,
};
const Wire = extern struct {
    opcode: u32, offset: u32, total: u32, length: u32, generation: u64,
    digest: [32]u8, root: Id, target: Id,
};
fn w(b: []const u8, i: usize) u32 { return std.mem.readInt(u32, b[i..][0..4], .little); }
fn q(b: []const u8, i: usize) u64 { return std.mem.readInt(u64, b[i..][0..8], .little); }
fn id(b: []const u8, i: usize) Id { return .{ .high = q(b,i), .low = q(b,i+8) }; }
fn valid(v: Id) bool { return v.high == 2 and v.low != 0; }
fn zero(v: Id) bool { return v.high == 0 and v.low == 0; }
fn equal(a: Id, b: Id) bool { return a.high == b.high and a.low == b.low; }
fn zeros(b: []const u8) bool { for (b) |x| if (x != 0) return false; return true; }

export fn migration_wire_decode(raw: ?[*]const u8, length: usize,
    kind: u32, out: ?*Wire) callconv(.c) c_int {
    const bytes = (raw orelse return 0);
    const output = out orelse return 0;
    if (length < 96 or length > 1024) return 0;
    const b = bytes[0..length];
    if (!std.mem.eql(u8,b[0..4],"JM5H") or b[4] != 1 or
        b[5] < 1 or b[5] > 6 or !zeros(b[6..8]) or !zeros(b[88..96])) return 0;
    const v: Wire = .{ .opcode = b[5], .offset = w(b,64), .total = w(b,68),
        .length = @intCast(length - 96), .generation = q(b,8), .digest = b[16..48].*,
        .root = id(b,48), .target = id(b,72) };
    if (!valid(v.root) or v.generation == 0 or v.total < 128 or
        v.total > 1048576 or v.offset > v.total) return 0;
    if (kind != (if (v.opcode >= 5) @as(u32,5) else @as(u32,4))) return 0;
    switch (v.opcode) {
        1 => if (v.offset != 0 or v.length != 0 or !zero(v.target)) return 0,
        2 => if (v.length == 0 or v.length > v.total-v.offset or !zero(v.target)) return 0,
        3 => if (v.offset != v.total or v.length != 0 or !zero(v.target)) return 0,
        4 => if (v.offset != 0 or v.length < 8 or v.length > 264 or !valid(v.target) or q(b,96) == 0) return 0,
        5 => if (v.length != 0 or (!zero(v.target) and (!valid(v.target) or v.offset != v.total))) return 0,
        6 => if (v.offset != 0 or v.length != 8 or !valid(v.target) or q(b,96) == 0) return 0,
        else => return 0,
    }
    output.* = v; return 1;
}
export fn migration_record_decode(raw: ?[*]const u8, length: usize,
    out: ?*Record) callconv(.c) c_int {
    const input = raw orelse return 0; const output = out orelse return 0;
    if (length != 176) return 0;
    const b = input[0..176];
    if (!std.mem.eql(u8,b[0..4],"JF5M") or b[4] != 1 or
        !zeros(b[5..8]) or !zeros(b[168..176])) return 0;
    const v: Record = .{ .peer = b[8..40].*, .digest = b[40..72].*,
        .root = id(b,72), .module = id(b,88), .state = id(b,104),
        .captable = id(b,120), .generation = q(b,136), .target = id(b,144),
        .forward_sequence = q(b,160) };
    if (!valid(v.root) or !valid(v.module) or !valid(v.state) or !valid(v.captable) or
        v.generation == 0 or (!zero(v.target) and !valid(v.target))) return 0;
    const ids = [_]Id{v.root,v.module,v.state,v.captable};
    for (ids,0..) |a,i| for (ids[0..i]) |c| if (equal(a,c)) return 0;
    if (zero(v.target) and v.forward_sequence != 0) return 0;
    output.* = v; return 1;
}
export fn migration_receipt_decode(raw: ?[*]const u8, length: usize,
    out: ?*Receipt) callconv(.c) c_int {
    const input = raw orelse return 0; const output = out orelse return 0;
    if (length != 128) return 0;
    const b = input[0..128];
    if (!std.mem.eql(u8,b[0..4],"JI5M") or b[4] != 1 or
        !zeros(b[5..8]) or !zeros(b[120..128])) return 0;
    const v: Receipt = .{ .peer = b[8..40].*, .digest = b[40..72].*,
        .source = id(b,72), .generation = q(b,88), .target = id(b,96),
        .forward_sequence = q(b,112) };
    if (!valid(v.source) or !valid(v.target) or v.generation == 0) return 0;
    output.* = v; return 1;
}
export fn migration_bundle_validate(raw: ?[*]const u8, length: usize,
    peer: ?*const [32]u8, local: ?*const [32]u8,
    out: ?*Bundle) callconv(.c) c_int {
    const input = raw orelse return 0; const output = out orelse return 0;
    const remote = peer orelse return 0; const own = local orelse return 0;
    if (length < 128 or length > 1048576) return 0;
    const b = input[0..length];
    if (!std.mem.eql(u8,b[0..4],"JB5M") or b[4] != 1 or !zeros(b[5..8]) or
        !std.mem.eql(u8,b[8..40],remote) or !std.mem.eql(u8,b[40..72],own) or
        !zeros(b[108..128])) return 0;
    const v: Bundle = .{ .root = id(b,72), .generation = q(b,88),
        .module_length = w(b,96), .state_length = w(b,100),
        .captable_length = w(b,104), .reserved = 0 };
    if (!valid(v.root) or v.generation == 0 or v.module_length < 8 or
        v.module_length > length-128 or v.captable_length != 544 or
        v.captable_length > length-128-v.module_length or
        v.state_length != length-128-v.module_length-v.captable_length) return 0;
    const caps = b[128+v.module_length..][0..544];
    if (q(caps,0) != 0x4A414E495F434150 or w(caps,8) != 3 or w(caps,12) > 16 or
        q(caps,16) == 0 or w(caps,28) != 0 or
        object_crc32c(caps.ptr+32,512) != w(caps,24)) return 0;
    var alive: usize = 0;
    for (0..16) |slot| {
        const cap = id(caps,32+slot*24);
        const rights = w(caps,48+slot*24);
        if (w(caps,416+slot*4) != 0xffffffff) return 0;
        if (zero(cap)) {
            if (rights != 0 or w(caps,52+slot*24) != 0) return 0;
        } else {
            if (slot >= w(caps,12) or !equal(cap,v.root) or
                rights == 0 or (rights & ~@as(u32,5)) != 0 or w(caps,480+slot*4) == 0) return 0;
            alive += 1;
        }
    }
    if (alive == 0) return 0;
    var sections: u32 = 0;
    if (jani_wasm_module_validate(b.ptr+128,v.module_length,&sections) == 0) return 0;
    const state = b[128+v.module_length+544..];
    if (instance_state_bytes_validate(state.ptr,state.len) == 0) return 0;
    const mailbox_size = w(state,36);
    var offset: usize = 64;
    while (offset < 64+mailbox_size) {
        const slot = w(state,offset+4);
        if (slot != 0xffffffff and zero(id(caps,32+@as(usize,slot)*24))) return 0;
        offset += 8+w(state,offset);
    }
    output.* = v; return 1;
}
