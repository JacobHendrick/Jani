const std = @import("std");
const ethernet = @import("ethernet");
const ipv4 = @import("ipv4");
const udp = @import("udp");

pub const panic = std.debug.no_panic;

pub const Datagram = struct {
    source: [4]u8,
    destination: [4]u8,
    source_port: u16,
    destination_port: u16,
    payload: []const u8,
};

pub const DecodeError = ethernet.DecodeError ||
    ipv4.DecodeError ||
    udp.DecodeError ||
    error{ UnsupportedEtherType, UnsupportedProtocol, BadUdpChecksum };

pub fn decode_udp_frame(frame: []const u8) DecodeError!Datagram {
    const eth = try ethernet.decode_header(frame);
    if (eth.ethertype != 0x0800) return error.UnsupportedEtherType;

    const ip = try ipv4.decode_packet(frame[ethernet.header_size..]);
    if (ip.protocol != 17) return error.UnsupportedProtocol;

    const header = try udp.decode_header(ip.payload);
    if (!udp.checksum_valid(ip.payload, ip.source, ip.destination)) return error.BadUdpChecksum;

    return Datagram{
        .source = ip.source,
        .destination = ip.destination,
        .source_port = header.source_port,
        .destination_port = header.destination_port,
        .payload = ip.payload[udp.header_size..],
    };
}

const CDecodedDatagram = extern struct {
    source: [4]u8,
    destination: [4]u8,
    source_port: u16,
    destination_port: u16,
    payload_offset: usize,
    payload_length: usize,
};

export fn jani_udp_frame_decode(
    raw: ?[*]const u8,
    length: usize,
    out: ?*CDecodedDatagram,
) callconv(.c) c_int {
    const input = raw orelse return 0;
    const output = out orelse return 0;

    const datagram = decode_udp_frame(input[0..length]) catch return 0;

    const payload_offset =
        @intFromPtr(datagram.payload.ptr) - @intFromPtr(input);

    output.* = CDecodedDatagram{
        .source = datagram.source,
        .destination = datagram.destination,
        .source_port = datagram.source_port,
        .destination_port = datagram.destination_port,
        .payload_offset = payload_offset,
        .payload_length = datagram.payload.len,
    };

    return 1;
}
