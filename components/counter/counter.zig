const jani = @import("jani");

const TICK_DELAY: u64 = 1;
const PROBE_BYTES: u32 = 64;

const JANI_EINVAL: i32 = -1;
const JANI_ERANGE: i32 = -5;
const CAP_RIGHT_READ: u32 = 0x01;
const CAP_RIGHT_ALL: u32 = 0x0f;

var counter: u64 = 0;

fn cell() *volatile u64 {
    return &counter;
}

fn say(message: []const u8) void {
    _ = jani.log(message);
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

    _ = jani.log(buffer[0..length]);
}

fn check(name: []const u8, ok: bool) void {
    say(if (ok) "syscall ok: " else "SYSCALL FAIL: ");
    say(name);
    say("\n");
}

fn probe_objects() void {
    var written: [8]u8 = .{ 1, 2, 3, 4, 5, 6, 7, 8 };
    var read_back: [8]u8 = .{ 0, 0, 0, 0, 0, 0, 0, 0 };

    const slot = jani.object_create(0, 7, PROBE_BYTES);
    if (slot < 0) {
        check("object_create", false);
        return;
    }
    check("object_create", true);

    check("object_size", jani.object_size(slot) == @as(i64, PROBE_BYTES));
    check("object_write", jani.object_write(slot, 4, &written) == 8);
    check("object_read", jani.object_read(slot, 4, &read_back) == 8);

    var matched = true;
    for (written, 0..) |value, index| {
        if (read_back[index] != value) {
            matched = false;
        }
    }
    check("object round trip", matched);

    check("object_read clamps", jani.object_read(slot, 60, &read_back) == 4);
    check("object_write bounds", jani.object_write(slot, 60, &written) == JANI_ERANGE);
    check("object_read past end", jani.object_read(slot, 0xFFFFFFFF, &read_back) == JANI_ERANGE);
    check("object_write past end", jani.object_write(slot, 0xFFFFFFFF, &written) == JANI_ERANGE);

    const child = jani.cap_derive(slot, CAP_RIGHT_READ, 42);
    check("cap_derive", child >= 0);
    if (child >= 0) {
        check("derived read", jani.object_size(child) == @as(i64, PROBE_BYTES));
        check("derived write denied", jani.object_write(child, 0, &written) < 0);
    }
    check("cap amplification denied", jani.cap_derive(slot, CAP_RIGHT_ALL, 0) < 0);
    check("cap_revoke", jani.cap_revoke(slot) == 0);
    check("revoked child rejected", child < 0 or jani.object_size(child) < 0);

    const drop_slot = jani.object_create(0, 8, 1);
    check("second object_create", drop_slot >= 0);
    if (drop_slot >= 0) {
        check("cap_drop", jani.cap_drop(drop_slot) == 0);
        check("dropped slot rejected", jani.object_size(drop_slot) < 0);
    }
}

fn probe_messages() void {
    const payload = "ping";
    var received: [8]u8 = .{ 0, 0, 0, 0, 0, 0, 0, 0 };
    var capability: i32 = 0;

    const me = jani.self();
    check("self", me >= 0);

    check("message_recv empty", jani.message_recv(&received, &capability) < 0);
    check("message_send", jani.message_send(me, payload, -1) == 0);

    const got = jani.message_recv(&received, &capability);
    check("message_recv", got == 4);
    check("message payload", received[0] == 'p' and received[3] == 'g');
    check("message capability", capability == -1);
}

fn probe_timer() void {
    const now: u64 = @intCast(jani.time_logical());
    const headroom: u64 = ~now;

    check("timer_set last deadline", jani.timer_set(headroom) == 0);
    check("timer_set rejects overflow", jani.timer_set(headroom + 1) == JANI_EINVAL);
}

export fn jani_init() void {
    cell().* = 0;

    check("time_logical", jani.time_logical() >= 0);
    probe_objects();
    probe_messages();

    _ = jani.timer_set(TICK_DELAY);
}

export fn jani_on_timer() void {
    const next = cell().* + 1;

    cell().* = next;
    if (next == 1) {
        probe_timer();
    }
    emit(next);
    _ = jani.timer_set(TICK_DELAY);
}

export fn jani_on_message() void {}
