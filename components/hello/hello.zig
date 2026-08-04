extern "env" fn jani_log(ptr: [*]const u8, len: u32) i32;

export fn run() void {
    const message = "hello from a WebAssembly module\n";
    _ = jani_log(message.ptr, message.len);
}
