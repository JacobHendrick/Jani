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

fn sum_words(bytes: []const u8) u32 {
    var total: u32 = 0;
    var at: usize = 0;

    while (at + 1 < bytes.len) : (at += 2) {
        total += std.mem.readInt(u16, bytes[at..][0..2], .big);
    }
    if (at < bytes.len) total += @as(u32, bytes[at]) << 8;
    return total;
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
