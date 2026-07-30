const record_size: usize = 512;
const record_magic: u64 = 0x4A414E4957414C31;
const record_format_version: u32 = 1;
const crc32c_polynomial: u32 = 0x82F63B78;

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

fn checksum(bytes: []const u8) u32 {
    var crc: u32 = 0xFFFF_FFFF;
    var index: usize = 0;

    while (index < bytes.len) : (index += 1) {
        var bit: u4 = 0;
        const byte: u8 = if ((index >= 40) and (index < 44)) 0 else bytes[index];

        crc ^= @as(u32, byte);
        while (bit < 8) : (bit += 1) {
            if ((crc & 1) != 0) {
                crc = (crc >> 1) ^ crc32c_polynomial;
            } else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

pub export fn object_wal_validate(
    bytes: ?[*]const u8,
    byte_count: usize,
) c_int {
    const raw = bytes orelse return 0;

    if (byte_count != record_size) {
        return 0;
    }

    const record = raw[0..record_size];

    if (read_u64_le(record, 0) != record_magic) {
        return 0;
    }

    if (read_u32_le(record, 8) != record_format_version) {
        return 0;
    }

    if ((read_u32_le(record, 12) != 0) or
        (read_u32_le(record, 44) != 0)) {
        return 0;
    }

    if ((read_u64_le(record, 16) == 0) or
        (read_u64_le(record, 24) == 0) or
        (read_u64_le(record, 32) == 0)) {
        return 0;
    }

    if (checksum(record) != read_u32_le(record, 40)) {
        return 0;
    }

    return 1;
}
