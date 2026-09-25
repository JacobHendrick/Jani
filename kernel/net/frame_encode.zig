const std = @import("std");
const ethernet = @import("ethernet");
const ipv4 = @import("ipv4");
const udp = @import("udp");

pub const panic = std.debug.no_panic;

pub const minimum_frame_size: usize = 60;
pub const maximum_frame_size: usize = 1514;
pub const frame_header_size: usize =
    ethernet.header_size + ipv4.header_size + udp.header_size;
pub const maximum_payload_size: usize =
    maximum_frame_size - frame_header_size;

pub const Fields = struct {
    destination_mac: [6]u8,
    source_mac: [6]u8,
    source_ip: [4]u8,
    destination_ip: [4]u8,
    source_port: u16,
    destination_port: u16,
    identification: u16,
    ttl: u8,
};

pub const EncodeError = ethernet.EncodeError ||
    ipv4.EncodeError ||
    udp.EncodeError ||
    error{ FrameTooLarge, OverlappingBuffers };

fn overlaps(output: []u8, frame_length: usize, payload: []const u8) bool {
    if (payload.len == 0) return false;

    const output_start = @intFromPtr(output.ptr);
    const payload_start = @intFromPtr(payload.ptr);

    if (output_start <= payload_start) {
        return payload_start - output_start < frame_length;
    }
    return output_start - payload_start < payload.len;
}

pub fn encode_udp_frame(
    output: []u8,
    fields: Fields,
    payload: []const u8,
) EncodeError!usize {
    if (payload.len > maximum_payload_size) return error.FrameTooLarge;

    const packet_length = frame_header_size + payload.len;
    const frame_length = @max(minimum_frame_size, packet_length);
    if (output.len < frame_length) return error.BufferTooSmall;
    if (overlaps(output, frame_length, payload)) {
        return error.OverlappingBuffers;
    }
    if (fields.ttl == 0) return error.InvalidTimeToLive;

    const ip_start = ethernet.header_size;
    const udp_start = ip_start + ipv4.header_size;
    const datagram = output[udp_start..packet_length];

    @memset(output[packet_length..frame_length], 0);
    @memcpy(datagram[udp.header_size..], payload);

    try udp.encode_header(datagram, .{
        .source = fields.source_ip,
        .destination = fields.destination_ip,
        .source_port = fields.source_port,
        .destination_port = fields.destination_port,
    });

    try ipv4.encode_header(output[ip_start..packet_length], .{
        .source = fields.source_ip,
        .destination = fields.destination_ip,
        .protocol = 17,
        .payload_length = datagram.len,
        .identification = fields.identification,
        .ttl = fields.ttl,
    });

    try ethernet.encode_header(output[0..ethernet.header_size], .{
        .destination = fields.destination_mac,
        .source = fields.source_mac,
        .ethertype = 0x0800,
    });

    return frame_length;
}
