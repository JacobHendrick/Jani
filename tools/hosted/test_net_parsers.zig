const std = @import("std");
const ethernet = @import("ethernet");
const ipv4 = @import("ipv4");
const udp = @import("udp");
const frame_decode = @import("frame_decode");

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

test "ethernet writes its fixed header atomically" {
    const header = ethernet.Header{
        .destination = .{ 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 },
        .source = .{ 0x52, 0x54, 0x00, 0xab, 0xcd, 0xef },
        .ethertype = 0x0800,
    };
    const expected = [_]u8{
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x52, 0x54, 0x00, 0xab, 0xcd, 0xef,
        0x08, 0x00,
    };
    var output = [_]u8{0xa5} ** (ethernet.header_size + 2);

    try ethernet.encode_header(&output, header);
    try std.testing.expectEqualSlices(
        u8,
        &expected,
        output[0..ethernet.header_size],
    );
    try std.testing.expectEqual(@as(u8, 0xa5), output[ethernet.header_size]);

    var rejected = [_]u8{0x5a} ** ethernet.header_size;
    const before = rejected;
    try std.testing.expectError(
        error.BufferTooSmall,
        ethernet.encode_header(rejected[0 .. ethernet.header_size - 1], header),
    );
    try std.testing.expectEqualSlices(u8, &before, &rejected);

    var unsupported = header;
    unsupported.ethertype = 0x05ff;
    try std.testing.expectError(
        error.UnsupportedFrameFormat,
        ethernet.encode_header(&rejected, unsupported),
    );
    try std.testing.expectEqualSlices(u8, &before, &rejected);
}

test "ipv4 writes a checksummed fixed header atomically" {
    const header = ipv4.Header{
        .source = .{ 1, 2, 3, 4 },
        .destination = .{ 5, 6, 7, 8 },
        .protocol = 17,
        .payload_length = 11,
        .identification = 0x1234,
        .ttl = 64,
    };
    var output = [_]u8{0xa5} ** (ipv4.header_size + 11);

    try ipv4.encode_header(&output, header);
    try std.testing.expectEqual(@as(u8, 0x45), output[0]);
    try std.testing.expectEqual(@as(u8, 0), output[1]);
    try std.testing.expectEqual(
        @as(u16, output.len),
        std.mem.readInt(u16, output[2..4], .big),
    );
    try std.testing.expectEqual(
        @as(u16, 0x1234),
        std.mem.readInt(u16, output[4..6], .big),
    );
    try std.testing.expectEqual(
        @as(u16, 0x4000),
        std.mem.readInt(u16, output[6..8], .big),
    );
    try std.testing.expectEqual(@as(u8, 64), output[8]);
    try std.testing.expectEqual(@as(u8, 17), output[9]);
    try std.testing.expect(ipv4.header_checksum_valid(output[0..20]));
    try std.testing.expectEqualSlices(u8, &header.source, output[12..16]);
    try std.testing.expectEqualSlices(u8, &header.destination, output[16..20]);
    try std.testing.expectEqual(@as(u8, 0xa5), output[20]);

    const packet = try ipv4.decode_packet(&output);
    try std.testing.expectEqualSlices(u8, &header.source, &packet.source);
    try std.testing.expectEqualSlices(u8, &header.destination, &packet.destination);
    try std.testing.expectEqualSlices(u8, output[20..], packet.payload);

    var rejected = [_]u8{0x5a} ** ipv4.header_size;
    const before = rejected;
    try std.testing.expectError(
        error.BufferTooSmall,
        ipv4.encode_header(rejected[0 .. ipv4.header_size - 1], header),
    );
    try std.testing.expectEqualSlices(u8, &before, &rejected);

    var invalid = header;
    invalid.payload_length = ipv4.maximum_payload_size + 1;
    try std.testing.expectError(
        error.PayloadTooLarge,
        ipv4.encode_header(&rejected, invalid),
    );
    try std.testing.expectEqualSlices(u8, &before, &rejected);

    invalid = header;
    invalid.ttl = 0;
    try std.testing.expectError(
        error.InvalidTimeToLive,
        ipv4.encode_header(&rejected, invalid),
    );
    try std.testing.expectEqualSlices(u8, &before, &rejected);
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

test "frame decoder exposes only a validated UDP payload" {
    var frame = frame_header ++ ip_packet ++ ([_]u8{0} ** 15);
    const datagram = try frame_decode.decode_udp_frame(&frame);
    try std.testing.expectEqualSlices(u8, &.{ 1, 2, 3, 4 }, &datagram.source);
    try std.testing.expectEqualSlices(u8, &.{ 5, 6, 7, 8 }, &datagram.destination);
    try std.testing.expectEqual(@as(u16, 1234), datagram.source_port);
    try std.testing.expectEqual(@as(u16, 5678), datagram.destination_port);
    try std.testing.expectEqualSlices(u8, "abc", datagram.payload);

    try std.testing.expectError(error.TruncatedHeader, frame_decode.decode_udp_frame(frame[0..13]));
    frame[12] = 0x86;
    frame[13] = 0xdd;
    try std.testing.expectError(error.UnsupportedEtherType, frame_decode.decode_udp_frame(&frame));
    frame[12] = 0x08;
    frame[13] = 0;

    frame[14 + 9] = 6;
    frame[14 + 11] = 0xc6;
    try std.testing.expectError(error.UnsupportedProtocol, frame_decode.decode_udp_frame(&frame));
    frame[14 + 9] = 17;
    frame[14 + 11] = 0xbb;

    frame[14 + 20 + 5] = 10;
    try std.testing.expectError(error.InvalidLength, frame_decode.decode_udp_frame(&frame));
    frame[14 + 20 + 5] = 11;
    frame[14 + 30] ^= 1;
    try std.testing.expectError(error.BadUdpChecksum, frame_decode.decode_udp_frame(&frame));
}
