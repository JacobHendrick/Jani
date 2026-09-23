const std = @import("std");

pub const header_size: usize = 20;
pub const maximum_payload_size: usize =
    std.math.maxInt(u16) - header_size;

pub const Header = struct {
    source: [4]u8,
    destination: [4]u8,
    protocol: u8,
    payload_length: usize,
    identification: u16,
    ttl: u8,
};

pub const EncodeError = error{
    BufferTooSmall,
    PayloadTooLarge,
    InvalidTimeToLive,
};

fn compute_header_checksum(header: []const u8) u16 {
    var sum: u32 = 0;
    var index: usize = 0;

    while (index < header.len) : (index += 2) {
        sum += std.mem.readInt(
            u16,
            header[index..][0..2],
            .big,
        );
    }

    while (sum > 0xffff) {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    const folded: u16 = @intCast(sum);
    return ~folded;
}

pub fn encode_header(
    bytes: []u8,
    header: Header,
) EncodeError!void {
    if (bytes.len < header_size) {
        return error.BufferTooSmall;
    }

    if (header.payload_length > maximum_payload_size) {
        return error.PayloadTooLarge;
    }

    if (header.ttl == 0) {
        return error.InvalidTimeToLive;
    }

    const total_length: u16 =
        @intCast(header_size + header.payload_length);

    @memset(bytes[0..header_size], 0);

    bytes[0] = 0x45;
    std.mem.writeInt(u16, bytes[2..4], total_length, .big);
    std.mem.writeInt(u16, bytes[4..6], header.identification, .big);
    std.mem.writeInt(u16, bytes[6..8], 0x4000, .big);

    bytes[8] = header.ttl;
    bytes[9] = header.protocol;
    bytes[12..16].* = header.source;
    bytes[16..20].* = header.destination;

    const checksum = compute_header_checksum(bytes[0..header_size]);
    std.mem.writeInt(u16, bytes[10..12], checksum, .big);
}

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
