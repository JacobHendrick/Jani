const header_size: usize = 128;
const header_magic: u64 = 0x4A414E495F4F424A;
const header_format_version: u32 = 1;

fn read_u32_le(bytes: []const u8, offset: usize) u32 {
    return @as(u32, bytes[offset]) |
        (@as(u32, bytes[offset + 1]) << 8) |
        (@as(u32, bytes[offset + 2]) << 16) |
        (@as(u32, bytes[offset + 3]) << 24);
}

fn read_u64_le(bytes: []const u8, offset: usize) u64 {
    return @as(u64, bytes[offset]) |
        (@as(u64, bytes[offset + 1]) << 8) |
        (@as(u64, bytes[offset + 2]) << 16) |
        (@as(u64, bytes[offset + 3]) << 24) |
        (@as(u64, bytes[offset + 4]) << 32) |
        (@as(u64, bytes[offset + 5]) << 40) |
        (@as(u64, bytes[offset + 6]) << 48) |
        (@as(u64, bytes[offset + 7]) << 56);
}

fn crc32c(bytes: []const u8) u32 {
    const polynomial: u32 = 0x82F63B78;
    var crc: u32 = 0xFFFF_FFFF;
    var index: usize = 0;

    while (index < bytes.len) : (index += 1) {
        var bit: u4 = 0;

        crc ^= @as(u32, bytes[index]);

        while (bit < 8) : (bit += 1) {
            if ((crc & 1) != 0) {
                crc = (crc >> 1) ^ polynomial;
            } else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

pub export fn object_crc32c(
    bytes: ?[*]const u8,
    byte_count: usize,
) u32 {
    const raw = bytes orelse return 0;

    return crc32c(raw[0..byte_count]);
}

pub export fn object_header_validate(
    bytes: ?[*]const u8,
    byte_count: usize,
) c_int {
    const raw = bytes orelse return 0;

    if (byte_count < header_size) {
        return 0;
    }

    const header = raw[0..header_size];

    if (read_u64_le(header, 0) != header_magic) {
        return 0;
    }

    if (read_u32_le(header, 8) != header_format_version) {
        return 0;
    }

    const id_high = read_u64_le(header, 16);
    const id_low = read_u64_le(header, 24);

    if ((id_high == 0) and (id_low == 0)) {
        return 0;
    }

    const type_id_high = read_u64_le(header, 32);
    const type_id_low = read_u64_le(header, 40);

    if ((type_id_high == 0) and (type_id_low == 0)) {
        return 0;
    }

    if (read_u64_le(header, 48) == 0) {
        return 0;
    }

    const payload_size = read_u64_le(header, 56);
    const available_payload_bytes: u64 =
        @intCast(byte_count - header_size);

    if (payload_size > available_payload_bytes) {
        return 0;
    }

    const payload_byte_count: usize = @intCast(payload_size);
    const expected_crc = read_u32_le(header, 64);
    const object_bytes = raw[0..byte_count];

    if (crc32c(object_bytes[header_size .. header_size + payload_byte_count]) !=
        expected_crc) {
        return 0;
    }

    if (read_u32_le(header, 68) != 0) {
        return 0;
    }

    if (read_u64_le(header, 112) != 0) {
        return 0;
    }

    if (read_u64_le(header, 120) != 0) {
        return 0;
    }

    return 1;
}
