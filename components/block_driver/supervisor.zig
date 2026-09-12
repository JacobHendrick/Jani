const jani = @import("jani");
export fn jani_on_message() void {
    _ = jani.driver_restart();
}
