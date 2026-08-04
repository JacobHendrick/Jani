extern "env" fn jani_log(ptr: [*]const u8, len: u32) i32;
extern "env" fn jani_timer_set(delay_ticks: i64) i32;

const TICK_DELAY: i64 = 1;

var counter: u64 = 0;

fn cell() *volatile u64 {
    return &counter;
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

export fn jani_init() void {
    cell().* = 0;
    _ = jani_timer_set(TICK_DELAY);
}

export fn jani_on_timer() void {
    const next = cell().* + 1;

    cell().* = next;
    emit(next);
    _ = jani_timer_set(TICK_DELAY);
}

export fn jani_on_message() void {}
