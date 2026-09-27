const std = @import("std");
pub const panic = std.debug.no_panic;

const Dot = extern struct { node: u32, removed: u32, counter: u64, value: u64 };
const State = extern struct {
    kind: u32, count: u32, vector: [2]u64, timestamp: u64,
    author: u32, length: u32, value: [256]u8, dots: [16]Dot,
};

fn dot_less(a: Dot, b: Dot) bool {
    return a.node < b.node or (a.node == b.node and a.counter < b.counter);
}
fn zero_dot(d: Dot) bool {
    return d.node == 0 and d.removed == 0 and d.counter == 0 and d.value == 0;
}
fn valid(s: State) bool {
    if (s.kind < 1 or s.kind > 2 or s.count > 16 or s.length > 256 or s.author > 1) return false;
    for (s.value[s.length..]) |byte| if (byte != 0) return false;
    for (s.dots[s.count..]) |dot| if (!zero_dot(dot)) return false;
    if (s.kind == 1) {
        if (s.count != 0) return false;
        if (s.timestamp == 0) return s.length == 0 and s.author == 0 and s.vector[0] == 0 and s.vector[1] == 0;
        return s.vector[s.author] != 0 and s.timestamp >= @max(s.vector[0], s.vector[1]);
    }
    if (s.timestamp != 0 or s.author != 0 or s.length != 0) return false;
    for (s.dots[0..s.count], 0..) |dot, i| {
        if (dot.node > 1 or dot.removed > 1 or dot.counter == 0 or dot.counter > s.vector[dot.node]) return false;
        if (i != 0 and !dot_less(s.dots[i-1], dot)) return false;
    }
    return true;
}
fn sort(s: *State) void {
    var i: usize = 1;
    while (i < s.count) : (i += 1) {
        const dot = s.dots[i];
        var at = i;
        while (at != 0 and dot_less(dot, s.dots[at-1])) : (at -= 1) {
            s.dots[at] = s.dots[at-1];
        }
        s.dots[at] = dot;
    }
}
export fn net_crdt_init(raw: ?*State, kind: u32) callconv(.c) c_int {
    const out = raw orelse return 0;
    var next = std.mem.zeroes(State);
    next.kind = kind;
    if (!valid(next)) return 0;
    out.* = next;
    return 1;
}
export fn net_crdt_set(raw: ?*State, node: u32, input: ?[*]const u8,
    length: usize) callconv(.c) c_int {
    const state = raw orelse return 0;
    var next = state.*;
    if (!valid(next) or next.kind != 1 or node > 1 or length > 256 or
        (input == null and length != 0) or next.timestamp == std.math.maxInt(u64) or
        next.vector[node] == std.math.maxInt(u64)) return 0;
    var value = [_]u8{0} ** 256;
    if (length != 0) @memcpy(value[0..length], input.?[0..length]);
    next.value = value;
    next.length = @intCast(length);
    next.author = node;
    next.timestamp += 1;
    next.vector[node] += 1;
    state.* = next;
    return 1;
}
export fn net_crdt_add(raw: ?*State, node: u32, value: u64) callconv(.c) c_int {
    const state = raw orelse return 0;
    var next = state.*;
    if (!valid(next) or next.kind != 2 or node > 1 or next.count == 16 or
        next.vector[node] == std.math.maxInt(u64)) return 0;
    next.vector[node] += 1;
    next.dots[next.count] = .{ .node = node, .removed = 0,
        .counter = next.vector[node], .value = value };
    next.count += 1;
    sort(&next);
    state.* = next;
    return 1;
}
export fn net_crdt_remove(raw: ?*State, node: u32, value: u64) callconv(.c) c_int {
    const state = raw orelse return 0;
    var next = state.*;
    if (!valid(next) or next.kind != 2 or node > 1 or
        next.vector[node] == std.math.maxInt(u64)) return 0;
    var changed = false;
    for (next.dots[0..next.count]) |*dot| {
        if (dot.value == value and dot.removed == 0) {
            dot.removed = 1;
            changed = true;
        }
    }
    if (changed) next.vector[node] += 1;
    state.* = next;
    return 1;
}
export fn net_crdt_merge(raw: ?*State, left: ?*const State,
    right: ?*const State) callconv(.c) c_int {
    const out = raw orelse return 0;
    const a = (left orelse return 0).*;
    const b = (right orelse return 0).*;
    if (!valid(a) or !valid(b) or a.kind != b.kind) return 0;
    var next = a;
    if (a.kind == 1) {
        if (a.timestamp == b.timestamp and a.author == b.author and
            (a.length != b.length or !std.mem.eql(u8, &a.value, &b.value))) return 0;
        if (b.timestamp > a.timestamp or (b.timestamp == a.timestamp and b.author > a.author)) next = b;
    } else {
        for (b.dots[0..b.count]) |dot| {
            var found = false;
            for (next.dots[0..next.count]) |*existing| {
                if (existing.node != dot.node or existing.counter != dot.counter) continue;
                if (existing.value != dot.value) return 0;
                existing.removed |= dot.removed;
                found = true;
                break;
            }
            if (!found) {
                if (next.count == 16) return 0;
                next.dots[next.count] = dot;
                next.count += 1;
            }
        }
        sort(&next);
    }
    for (0..2) |i| next.vector[i] = @max(a.vector[i], b.vector[i]);
    out.* = next;
    return 1;
}
export fn net_crdt_encode(raw: ?[*]u8, capacity: usize,
    input: ?*const State) callconv(.c) usize {
    const out = raw orelse return 0;
    const state = (input orelse return 0).*;
    if (capacity < 680 or !valid(state)) return 0;
    var bytes = [_]u8{0} ** 680;
    @memcpy(bytes[0..4], "JC5R");
    bytes[4] = 1;
    bytes[5] = @intCast(state.kind);
    std.mem.writeInt(u16, bytes[6..8], @intCast(state.count), .little);
    std.mem.writeInt(u64, bytes[8..16], state.vector[0], .little);
    std.mem.writeInt(u64, bytes[16..24], state.vector[1], .little);
    std.mem.writeInt(u64, bytes[24..32], state.timestamp, .little);
    std.mem.writeInt(u32, bytes[32..36], state.author, .little);
    std.mem.writeInt(u32, bytes[36..40], state.length, .little);
    @memcpy(bytes[40..296], &state.value);
    for (state.dots, 0..) |dot, i| {
        const at = 296 + 24*i;
        std.mem.writeInt(u32, bytes[at..][0..4], dot.node, .little);
        std.mem.writeInt(u32, bytes[at+4..][0..4], dot.removed, .little);
        std.mem.writeInt(u64, bytes[at+8..][0..8], dot.counter, .little);
        std.mem.writeInt(u64, bytes[at+16..][0..8], dot.value, .little);
    }
    @memcpy(out[0..680], &bytes);
    return 680;
}
export fn net_crdt_decode(raw: ?[*]const u8, length: usize,
    output: ?*State) callconv(.c) c_int {
    const input = raw orelse return 0;
    const out = output orelse return 0;
    if (length != 680) return 0;
    const bytes = input[0..680];
    if (!std.mem.eql(u8, bytes[0..4], "JC5R") or bytes[4] != 1) return 0;
    var next = std.mem.zeroes(State);
    next.kind = bytes[5];
    next.count = std.mem.readInt(u16, bytes[6..8], .little);
    next.vector[0] = std.mem.readInt(u64, bytes[8..16], .little);
    next.vector[1] = std.mem.readInt(u64, bytes[16..24], .little);
    next.timestamp = std.mem.readInt(u64, bytes[24..32], .little);
    next.author = std.mem.readInt(u32, bytes[32..36], .little);
    next.length = std.mem.readInt(u32, bytes[36..40], .little);
    @memcpy(&next.value, bytes[40..296]);
    for (&next.dots, 0..) |*dot, i| {
        const at = 296 + 24*i;
        dot.* = .{
            .node = std.mem.readInt(u32, bytes[at..][0..4], .little),
            .removed = std.mem.readInt(u32, bytes[at+4..][0..4], .little),
            .counter = std.mem.readInt(u64, bytes[at+8..][0..8], .little),
            .value = std.mem.readInt(u64, bytes[at+16..][0..8], .little),
        };
    }
    if (!valid(next)) return 0;
    out.* = next;
    return 1;
}
