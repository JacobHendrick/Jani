const std = @import("std");

pub fn header_checksum_valid(header: []const u8) bool {
    if (header.len < 20 or header.len > 60 or header.len % 4 != 0) {
        return false;
    }

    var sum: u32 = 0;
    var index: usize = 0;

    while (index < header.len) : (index += 2) {
        sum += std.mem.readInt(u16, header[index..][0..2], .big);
    }

    while (sum > 0xffff) {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    return sum == 0xffff;
}

pub const Packet = struct {
    source: [4]u8,
    destination: [4]u8,
    protocol: u8,
    payload: []const u8,
};

pub const DecodeError = error{
    TruncatedHeader,
    UnsupportedHeader,
    InvalidLength,
    InvalidFlags,
    FragmentedPacket,
    ExpiredPacket,
    BadChecksum,
};

pub fn decode_packet(bytes: []const u8) DecodeError!Packet {
    if (bytes.len < 20) return error.TruncatedHeader;
    if (bytes[0] != 0x45) return error.UnsupportedHeader;

    const total_length: usize = std.mem.readInt(u16, bytes[2..4], .big);
    if (total_length < 20 or total_length > bytes.len) {
        return error.InvalidLength;
    }

    const flags_and_offset = std.mem.readInt(u16, bytes[6..8], .big);
    if ((flags_and_offset & 0x8000) != 0) return error.InvalidFlags;
    if ((flags_and_offset & 0x3fff) != 0) return error.FragmentedPacket;
    if (bytes[8] == 0) return error.ExpiredPacket;
    if (!header_checksum_valid(bytes[0..20])) return error.BadChecksum;

    return .{
        .source = bytes[12..16].*,
        .destination = bytes[16..20].*,
        .protocol = bytes[9],
        .payload = bytes[20..total_length],
    };
}
