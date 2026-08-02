const header_size: usize = 8;
const magic: [4]u8 = .{ 0x00, 0x61, 0x73, 0x6d };
const format_version: u32 = 1;

const section_custom: u8 = 0;
const section_max_id: u8 = 12;
const leb128_max_bytes: usize = 5;

const LengthResult = struct {
    value: u32,
    next: usize,
};

fn read_u32_le(bytes: []const u8, offset: usize) u32 {
    return @as(u32, bytes[offset]) |
        (@as(u32, bytes[offset + 1]) << 8) |
        (@as(u32, bytes[offset + 2]) << 16) |
        (@as(u32, bytes[offset + 3]) << 24);
}

fn read_leb128_u32(bytes: []const u8, offset: usize) ?LengthResult {
    var value: u64 = 0;
    var shift: u6 = 0;
    var index: usize = offset;
    var consumed: usize = 0;

    while (consumed < leb128_max_bytes) {
        if (index >= bytes.len) {
            return null;
        }

        const byte = bytes[index];
        value |= @as(u64, byte & 0x7F) << shift;

        index += 1;
        consumed += 1;

        if ((byte & 0x80) == 0) {
            if (value > 0xFFFF_FFFF) {
                return null;
            }
            if ((consumed > 1) and (byte == 0)) {
                return null;
            }
            return LengthResult{ .value = @intCast(value), .next = index };
        }

        shift += 7;
    }

    return null;
}

export fn jani_wasm_module_validate(
    bytes: [*c]const u8,
    byte_count: usize,
    section_count_out: [*c]u32,
) callconv(.c) c_int {
    if (bytes == null) {
        return 0;
    }
    if (byte_count < header_size) {
        return 0;
    }

    const module = bytes[0..byte_count];

    var index: usize = 0;
    while (index < magic.len) : (index += 1) {
        if (module[index] != magic[index]) {
            return 0;
        }
    }

    if (read_u32_le(module, 4) != format_version) {
        return 0;
    }

    var seen: [section_max_id + 1]bool = .{false} ** (section_max_id + 1);
    var highest_seen: u8 = 0;
    var sections: u32 = 0;
    var cursor: usize = header_size;

    while (cursor < byte_count) {
        const id = module[cursor];
        cursor += 1;

        if (id > section_max_id) {
            return 0;
        }

        if (id != section_custom) {
            if (seen[id]) {
                return 0;
            }
            if (id < highest_seen) {
                return 0;
            }
            seen[id] = true;
            highest_seen = id;
        }

        const length = read_leb128_u32(module, cursor) orelse return 0;
        cursor = length.next;

        if (length.value > (byte_count - cursor)) {
            return 0;
        }

        cursor += length.value;
        sections += 1;
    }

    if (cursor != byte_count) {
        return 0;
    }

    if (section_count_out != null) {
        section_count_out.* = sections;
    }

    return 1;
}
