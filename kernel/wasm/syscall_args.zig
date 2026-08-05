const slot_none: i32 = -1;

fn span_fits(limit: u64, start: u32, length: u32) bool {
    const end: u64 = @as(u64, start) + @as(u64, length);

    if (@as(u64, start) > limit) {
        return false;
    }

    return end <= limit;
}

export fn jani_syscall_check_span(
    limit: u64,
    start: u32,
    length: u32,
) callconv(.c) c_int {
    return if (span_fits(limit, start, length)) 1 else 0;
}

export fn jani_syscall_check_slot(
    slot_count: u32,
    slot: i32,
) callconv(.c) c_int {
    if (slot < 0) {
        return 0;
    }

    return if (@as(u32, @intCast(slot)) < slot_count) 1 else 0;
}

export fn jani_syscall_check_optional_slot(
    slot_count: u32,
    slot: i32,
) callconv(.c) c_int {
    if (slot == slot_none) {
        return 1;
    }

    return jani_syscall_check_slot(slot_count, slot);
}

export fn jani_syscall_clamp_read(
    object_size: u64,
    offset: u32,
    length: u32,
    length_out: [*c]u32,
) callconv(.c) c_int {
    if (length_out == null) {
        return 0;
    }

    if (@as(u64, offset) > object_size) {
        length_out.* = 0;
        return 0;
    }

    const remaining: u64 = object_size - @as(u64, offset);
    const clamped: u64 = if (@as(u64, length) < remaining) @as(u64, length) else remaining;

    length_out.* = @intCast(clamped);
    return 1;
}

export fn jani_syscall_deadline(
    logical_time: u64,
    delay_ticks: u64,
    deadline_out: [*c]u64,
) callconv(.c) c_int {
    if (deadline_out == null) {
        return 0;
    }

    const sum = @addWithOverflow(logical_time, delay_ticks);
    if (sum[1] != 0) {
        return 0;
    }

    deadline_out.* = sum[0];
    return 1;
}

export fn jani_syscall_check_transfer(
    memory_size: u64,
    pointer: u32,
    object_size: u64,
    offset: u32,
    length: u32,
) callconv(.c) c_int {
    if (!span_fits(memory_size, pointer, length)) {
        return 0;
    }

    return if (span_fits(object_size, offset, length)) 1 else 0;
}
