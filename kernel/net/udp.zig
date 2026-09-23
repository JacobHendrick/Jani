const std = @import("std");

pub const header_size: usize = 8;

pub const Header = struct {
    source_port: u16,
    destination_port: u16,
    length: usize,
    checksum: u16,
};

pub const DecodeError = error{
    TruncatedHeader,
    InvalidLength,
};

pub const EncodeError = error{
    BufferTooSmall,
    DatagramTooLarge,
};

pub fn decode_header(bytes: []const u8) DecodeError!Header {
    if (bytes.len < header_size) {
        return error.TruncatedHeader;
    }

    const length: usize = std.mem.readInt(u16, bytes[4..6], .big);
    if (length < header_size or length != bytes.len) {
        return error.InvalidLength;
    }

    return .{
        .source_port = std.mem.readInt(u16, bytes[0..2], .big),
        .destination_port = std.mem.readInt(u16, bytes[2..4], .big),
        .length = length,
        .checksum = std.mem.readInt(u16, bytes[6..8], .big),
    };
}

pub const maximum_datagram_size: usize = std.math.maxInt(u16);
pub const maximum_payload_size: usize =
    maximum_datagram_size - header_size;

pub const EncodeHeader = struct {
    source: [4]u8,
    destination: [4]u8,
    source_port: u16,
    destination_port: u16,
};

fn sum_words(bytes: []const u8) u32 {
    var total: u32 = 0;
    var at: usize = 0;

    while (at + 1 < bytes.len) : (at += 2) {
        total += std.mem.readInt(u16, bytes[at..][0..2], .big);
    }
    if (at < bytes.len) total += @as(u32, bytes[at]) << 8;
    return total;
}

pub fn encode_header(
    bytes: []u8,
    header: EncodeHeader,
) EncodeError!void {
    if (bytes.len < header_size) {
        return error.BufferTooSmall;
    }

    if (bytes.len > maximum_datagram_size) {
        return error.DatagramTooLarge;
    }

    const datagram_length: u16 = @intCast(bytes.len);

    @memset(bytes[0..header_size], 0);

    std.mem.writeInt(
        u16,
        bytes[0..2],
        header.source_port,
        .big,
    );

    std.mem.writeInt(
        u16,
        bytes[2..4],
        header.destination_port,
        .big,
    );

    std.mem.writeInt(
        u16,
        bytes[4..6],
        datagram_length,
        .big,
    );

    var sum = sum_words(&header.source) +
        sum_words(&header.destination);

    sum += 17;
    sum += @as(u32, datagram_length);
    sum += sum_words(bytes);

    while (sum > 0xffff) {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    const folded: u16 = @intCast(sum);
    var checksum = ~folded;

    if (checksum == 0) {
        checksum = 0xffff;
    }

    std.mem.writeInt(u16, bytes[6..8], checksum, .big);
}

pub fn checksum_valid(bytes: []const u8, source: [4]u8, destination: [4]u8) bool {
    _ = decode_header(bytes) catch return false;
    if (std.mem.readInt(u16, bytes[6..8], .big) == 0) return false;

    var sum = sum_words(&source) + sum_words(&destination);
    sum += 17;
    sum += @as(u32, @intCast(bytes.len));
    sum += sum_words(bytes);

    while (sum > 0xffff) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return sum == 0xffff;
}
