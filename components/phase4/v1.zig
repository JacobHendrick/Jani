const service = @import("service.zig");
export fn jani_init() void {
    service.init();
}
export fn jani_on_message() void {
    service.message(1);
}
export fn jani_on_timer() void {}
