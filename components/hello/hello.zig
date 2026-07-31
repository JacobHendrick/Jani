extern "env" fn jani_log(ptr: [*]const u8, len: u32) void;

export fn run() void {
    const message = "hello from a WebAssembly module\n";
    jani_log(message.ptr, message.len);
}
