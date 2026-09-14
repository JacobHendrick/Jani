const std = @import("std");

pub const header_size: usize = 14;

pub const Header = struct {
    destination: [6]u8,
    source: [6]u8,
    ethertype: u16,
};

pub const DecodeError = error{
    TruncatedHeader,
    UnsupportedFrameFormat,
};

pub fn decode_header(frame: []const u8) DecodeError!Header {
    if (frame.len < header_size) {
        return error.TruncatedHeader;
    }

    const ether_type = std.mem.readInt(u16, frame[12..14], .big);

    if (ether_type < 0x0600) {
        return error.UnsupportedFrameFormat;
    }

    return .{
        .destination = frame[0..6].*,
        .source = frame[6..12].*,
        .ethertype = ether_type,
    };
}
