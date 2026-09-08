const jani = @import("jani");
const std = @import("std");
pub const panic = std.debug.no_panic;

export fn jani_on_message() void {
    var request: [528]u8 = undefined;
    var data: [512]u8 = undefined;
    var status: [1]u8 = .{255};
    if (jani.driver_request(&request) != request.len) return;
    const operation = std.mem.readInt(u32, request[0..4], .little);
    if (operation != 0 and operation != 1 and operation != 4) return;
    if (jani.dma_write(0, 0, request[0..16]) != 16 or jani.dma_write(0, 16, &status) != 1) return;
    if (operation == 1 and jani.dma_write(0, 512, request[16..]) != 512) return;
    const normal = [_]u32{ 0, 16, 0, 512, 512, if (operation == 0) 1 else 0, 16, 1, 1 };
    const flush = [_]u32{ 0, 16, 0, 16, 1, 1 };
    const descriptors = if (operation == 4) std.mem.sliceAsBytes(&flush) else std.mem.sliceAsBytes(&normal);
    if (jani.queue_submit(0, descriptors) != 0 or jani.dma_read(0, 16, &status) != 1 or status[0] != 0) {
        _ = jani.driver_complete(-1, &.{});
        return;
    }
    if (operation == 0) {
        if (jani.dma_read(0, 512, &data) != 512) return;
        _ = jani.driver_complete(0, &data);
    } else {
        _ = jani.driver_complete(0, &.{});
    }
}

export fn jani_kill() void {
    @trap();
}
