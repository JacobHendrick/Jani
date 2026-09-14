const std = @import("std");
const ethernet = @import("ethernet");
const ipv4 = @import("ipv4");
const udp = @import("udp");

const frame_header = [_]u8{
    0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
    0x52, 0x54, 0x00, 0xab, 0xcd, 0xef,
    0x08, 0x00,
};
const ip_packet = [_]u8{
    0x45, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x11, 0x6a, 0xbb, 0x01, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0x04, 0xd2, 0x16, 0x2e,
    0x00, 0x0b, 0x10, 0x62, 0x61, 0x62, 0x63,
};

test "ethernet validates its fixed header" {
    try std.testing.expectError(error.TruncatedHeader, ethernet.decode_header(frame_header[0..13]));
    const header = try ethernet.decode_header(&frame_header);
    try std.testing.expectEqualSlices(u8, &.{ 0x52, 0x54, 0, 0x12, 0x34, 0x56 }, &header.destination);
    try std.testing.expectEqualSlices(u8, &.{ 0x52, 0x54, 0, 0xab, 0xcd, 0xef }, &header.source);
    try std.testing.expectEqual(@as(u16, 0x0800), header.ethertype);

    var short_type = frame_header;
    short_type[12] = 0x05;
    short_type[13] = 0xff;
    try std.testing.expectError(error.UnsupportedFrameFormat, ethernet.decode_header(&short_type));
}

test "ipv4 trims padding and validates the header" {
    var padded = ip_packet ++ ([_]u8{0} ** 15);
    const packet = try ipv4.decode_packet(&padded);
    try std.testing.expectEqualSlices(u8, &.{ 1, 2, 3, 4 }, &packet.source);
    try std.testing.expectEqualSlices(u8, &.{ 5, 6, 7, 8 }, &packet.destination);
    try std.testing.expectEqual(@as(u8, 17), packet.protocol);
    try std.testing.expectEqualSlices(u8, ip_packet[20..], packet.payload);
    try std.testing.expect(ipv4.header_checksum_valid(ip_packet[0..20]));
    try std.testing.expect(!ipv4.header_checksum_valid(ip_packet[0..19]));

    try std.testing.expectError(error.TruncatedHeader, ipv4.decode_packet(ip_packet[0..19]));
    padded[0] = 0x65;
    try std.testing.expectError(error.UnsupportedHeader, ipv4.decode_packet(&padded));
    padded[0] = 0x46;
    try std.testing.expectError(error.UnsupportedHeader, ipv4.decode_packet(&padded));
    padded[0] = 0x45;
    padded[3] = 19;
    try std.testing.expectError(error.InvalidLength, ipv4.decode_packet(&padded));
    padded[3] = 61;
    try std.testing.expectError(error.InvalidLength, ipv4.decode_packet(&padded));
    padded[3] = 31;
    padded[6] = 0x80;
    try std.testing.expectError(error.InvalidFlags, ipv4.decode_packet(&padded));
    padded[6] = 0;
    padded[8] = 0;
    try std.testing.expectError(error.ExpiredPacket, ipv4.decode_packet(&padded));
    padded[8] = 0x40;
    padded[10] = 0;
    try std.testing.expectError(error.BadChecksum, ipv4.decode_packet(&padded));
}

test "ipv4 rejects first fragment" {
    var packet = ip_packet;
    packet[6] = 0x20;
    packet[10] = 0x4a;
    try std.testing.expect(ipv4.header_checksum_valid(packet[0..20]));
    try std.testing.expectError(error.FragmentedPacket, ipv4.decode_packet(&packet));
    packet[6] = 0x40;
    packet[10] = 0x2a;
    try std.testing.expect(ipv4.header_checksum_valid(packet[0..20]));
    _ = try ipv4.decode_packet(&packet);
    packet[6] = 0;
    packet[7] = 1;
    try std.testing.expectError(error.FragmentedPacket, ipv4.decode_packet(&packet));
}

test "udp validates length, ports, and pseudo-header checksum" {
    const packet = try ipv4.decode_packet(&ip_packet);
    const header = try udp.decode_header(packet.payload);
    try std.testing.expectEqual(@as(u16, 1234), header.source_port);
    try std.testing.expectEqual(@as(u16, 5678), header.destination_port);
    try std.testing.expectEqual(@as(usize, 11), header.length);
    try std.testing.expectEqual(@as(u16, 0x1062), header.checksum);
    try std.testing.expect(udp.checksum_valid(packet.payload, packet.source, packet.destination));
    var different_source = packet.source;
    different_source[0] ^= 1;
    try std.testing.expect(!udp.checksum_valid(packet.payload, different_source, packet.destination));

    try std.testing.expectError(error.TruncatedHeader, udp.decode_header(packet.payload[0..7]));
    var datagram = ip_packet[20..].*;
    datagram[5] = 7;
    try std.testing.expectError(error.InvalidLength, udp.decode_header(&datagram));
    datagram[5] = 10;
    try std.testing.expectError(error.InvalidLength, udp.decode_header(&datagram));
    datagram[5] = 12;
    try std.testing.expectError(error.InvalidLength, udp.decode_header(&datagram));
    datagram[5] = 11;
    datagram[10] ^= 1;
    try std.testing.expect(!udp.checksum_valid(&datagram, packet.source, packet.destination));
    datagram[10] ^= 1;
    datagram[6] = 0;
    datagram[7] = 0;
    try std.testing.expect(!udp.checksum_valid(&datagram, packet.source, packet.destination));
}
