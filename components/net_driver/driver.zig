const std = @import("std");
const jani = @import("jani");
const frame_decode = @import("frame_decode");
pub const panic = std.debug.no_panic;

const Result = extern struct {
    source: u32, destination: u32,
    source_port: u32, destination_port: u32,
    payload_offset: u32, payload_length: u32, reserved: u32,
};
comptime { if (@sizeOf(Result) != 28) @compileError("network response layout changed"); }

export fn jani_on_message() void {
    var frame: [1514]u8 = undefined;
    const length = jani.net_driver_receive(&frame);
    if (length < 14 or length > frame.len) {
        _ = jani.net_driver_complete(-1, &.{});
        return;
    }
    const bytes = frame[0..@intCast(length)];
    const packet = frame_decode.decode_udp_frame(bytes) catch {
        _ = jani.net_driver_complete(-1, &.{});
        return;
    };
    var result: Result = .{
        .source = std.mem.readInt(u32, &packet.source, .little),
        .destination = std.mem.readInt(u32, &packet.destination, .little),
        .source_port = packet.source_port, .destination_port = packet.destination_port,
        .payload_offset = @intCast(@intFromPtr(packet.payload.ptr)-@intFromPtr(bytes.ptr)),
        .payload_length = @intCast(packet.payload.len), .reserved = 0,
    };
    _ = jani.net_driver_complete(0, std.mem.asBytes(&result));
}
