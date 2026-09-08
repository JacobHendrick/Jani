const std = @import("std");
pub const panic = std.debug.no_panic;
const header_size = 24;
const max_records = 64;
const ref_size = 24;
const record_size = 48;
const total_size = header_size + max_records * record_size;

extern fn object_crc32c(bytes: [*]const u8, length: usize) u32;

fn u32le(bytes: []const u8, offset: usize) u32 {
    return std.mem.readInt(u32, bytes[offset..][0..4], .little);
}

fn u64le(bytes: []const u8, offset: usize) u64 {
    return std.mem.readInt(u64, bytes[offset..][0..8], .little);
}

fn validRef(bytes: []const u8) bool {
    return (u64le(bytes, 0) != 0 or u64le(bytes, 8) != 0) and
        u32le(bytes, 16) < 16 and u32le(bytes, 20) != 0;
}

fn parent(bytes: []const u8, index: usize) []const u8 {
    const offset = header_size + index * record_size;
    return bytes[offset..][0..ref_size];
}

fn child(bytes: []const u8, index: usize) []const u8 {
    const offset = header_size + index * record_size + ref_size;
    return bytes[offset..][0..ref_size];
}

pub export fn capability_lineage_validate(raw: ?[*]const u8, length: usize) c_int {
    const pointer = raw orelse return 0;
    if (length != total_size) return 0;
    const bytes = pointer[0..total_size];
    if (u64le(bytes, 0) != 0x4A414E494C494E31 or
        u32le(bytes, 8) != 1 or u32le(bytes, 20) != 0) return 0;
    const count = u32le(bytes, 12);
    if (count > max_records) return 0;
    if (object_crc32c(bytes[header_size..].ptr, total_size - header_size) !=
        u32le(bytes, 16)) return 0;
    for (bytes[header_size + count * record_size ..]) |byte| {
        if (byte != 0) return 0;
    }
    for (0..count) |index| {
        const p = parent(bytes, index);
        const c = child(bytes, index);
        if (!validRef(p) or !validRef(c) or std.mem.eql(u8, p, c)) return 0;
        for (0..index) |other| {
            if (std.mem.eql(u8, c, child(bytes, other))) return 0;
        }
        var current = p;
        var depth: usize = 0;
        while (depth < count) : (depth += 1) {
            if (std.mem.eql(u8, current, c)) return 0;
            var found = false;
            for (0..count) |other| {
                if (std.mem.eql(u8, current, child(bytes, other))) {
                    current = parent(bytes, other);
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }
        if (depth == count) return 0;
    }
    return 1;
}
