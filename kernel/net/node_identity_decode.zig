const std = @import("std");
pub const panic = std.debug.no_panic;

pub const public_key_size: usize = 32;

pub const PublicKey = [public_key_size]u8;

pub const DecodeError = error{
    InvalidPublicKey,
};

pub fn decode_public_key(bytes: []const u8) DecodeError!PublicKey {
    if (bytes.len != public_key_size) {
        return DecodeError.InvalidPublicKey;
    }

    return bytes[0..public_key_size].*;
}

const CPublicKey = extern struct {
    bytes: PublicKey,
};

pub export fn node_public_key_decode(
    raw: ?[*]const u8,
    length: usize,
    out: ?*CPublicKey,
) c_int {
    const input = raw orelse return 0;
    const output = out orelse return 0;
    if (length != public_key_size) return 0;

    // Check the length before making a slice from a C pointer.
    const key = decode_public_key(input[0..public_key_size]) catch return 0;
    output.bytes = key;
    return 1;
}

const testing = std.testing;

test "decoder copies exactly 32 bytes" {
    var input = [_]u8{7} ** public_key_size;
    input[0] = 1;
    input[public_key_size - 1] = 255;

    const key = try decode_public_key(&input);

    try testing.expectEqualSlices(u8, &input, &key);

    input[0] = 9;
    try testing.expect(key[0] == 1);
}

test "decoder rejects incorrect lengths" {
    const input = [_]u8{0} ** (public_key_size + 1);

    try testing.expectError(
        error.InvalidPublicKey,
        decode_public_key(input[0..0]),
    );
    try testing.expectError(
        error.InvalidPublicKey,
        decode_public_key(input[0..(public_key_size - 1)]),
    );
    try testing.expectError(
        error.InvalidPublicKey,
        decode_public_key(&input),
    );
}
