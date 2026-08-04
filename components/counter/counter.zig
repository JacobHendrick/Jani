extern "env" fn jani_log(ptr: [*]const u8, len: u32) i32;
extern "env" fn jani_timer_set(delay_ticks: i64) i32;
extern "env" fn jani_object_create(type_high: i64, type_low: i64, size: i32) i32;
extern "env" fn jani_object_read(slot: i32, offset: i32, ptr: [*]u8, len: u32) i32;
extern "env" fn jani_object_write(slot: i32, offset: i32, ptr: [*]const u8, len: u32) i32;
extern "env" fn jani_object_size(slot: i32) i64;
extern "env" fn jani_cap_drop(slot: i32) i32;
extern "env" fn jani_message_send(target: i32, ptr: [*]const u8, len: u32, cap: i32) i32;
extern "env" fn jani_message_recv(ptr: [*]u8, len: u32, cap_out: ?*i32) i32;
extern "env" fn jani_time_logical() i64;
extern "env" fn jani_self() i32;

const TICK_DELAY: i64 = 1;
const PROBE_BYTES: i32 = 64;

var counter: u64 = 0;

fn cell() *volatile u64 {
    return &counter;
}

fn say(message: []const u8) void {
    _ = jani_log(message.ptr, @intCast(message.len));
}

fn emit(value: u64) void {
    const prefix = "counter: ";
    var buffer: [32]u8 = undefined;
    var digits: [20]u8 = undefined;
    var digit_count: usize = 0;
    var length: usize = 0;
    var remaining = value;

    if (remaining == 0) {
        digits[0] = '0';
        digit_count = 1;
    } else {
        while (remaining != 0) {
            digits[digit_count] = '0' + @as(u8, @intCast(remaining % 10));
            digit_count += 1;
            remaining /= 10;
        }
    }

    for (prefix) |character| {
        buffer[length] = character;
        length += 1;
    }

    while (digit_count > 0) {
        digit_count -= 1;
        buffer[length] = digits[digit_count];
        length += 1;
    }

    buffer[length] = '\n';
    length += 1;

    _ = jani_log(&buffer, @intCast(length));
}

fn check(name: []const u8, ok: bool) void {
    say(if (ok) "syscall ok: " else "SYSCALL FAIL: ");
    say(name);
    say("\n");
}

fn probe_objects() void {
    var written: [8]u8 = .{ 1, 2, 3, 4, 5, 6, 7, 8 };
    var read_back: [8]u8 = .{ 0, 0, 0, 0, 0, 0, 0, 0 };

    const slot = jani_object_create(0, 7, PROBE_BYTES);
    if (slot < 0) {
        check("object_create", false);
        return;
    }
    check("object_create", true);

    check("object_size", jani_object_size(slot) == PROBE_BYTES);
    check("object_write", jani_object_write(slot, 4, &written, 8) == 8);
    check("object_read", jani_object_read(slot, 4, &read_back, 8) == 8);

    var matched = true;
    for (written, 0..) |value, index| {
        if (read_back[index] != value) {
            matched = false;
        }
    }
    check("object round trip", matched);

    check("object_read clamps", jani_object_read(slot, 60, &read_back, 8) == 4);
    check("object_write bounds", jani_object_write(slot, 60, &written, 8) < 0);
    check("cap_drop", jani_cap_drop(slot) == 0);
    check("dropped slot rejected", jani_object_size(slot) < 0);
}

fn probe_messages() void {
    const payload = "ping";
    var received: [8]u8 = .{ 0, 0, 0, 0, 0, 0, 0, 0 };
    var capability: i32 = 0;

    const me = jani_self();
    check("self", me >= 0);

    check("message_recv empty", jani_message_recv(&received, 8, &capability) < 0);
    check("message_send", jani_message_send(me, payload.ptr, payload.len, -1) == 0);

    const got = jani_message_recv(&received, 8, &capability);
    check("message_recv", got == 4);
    check("message payload", received[0] == 'p' and received[3] == 'g');
    check("message capability", capability == -1);
}

export fn jani_init() void {
    cell().* = 0;

    check("time_logical", jani_time_logical() >= 0);
    probe_objects();
    probe_messages();

    _ = jani_timer_set(TICK_DELAY);
}

export fn jani_on_timer() void {
    const next = cell().* + 1;

    cell().* = next;
    emit(next);
    _ = jani_timer_set(TICK_DELAY);
}

export fn jani_on_message() void {}
