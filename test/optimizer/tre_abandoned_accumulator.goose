// An argument return abandons this frame's pending addition. Only outer
// recursive calls contribute to the result.
recursive fn leave_arg(n: i64) -> i64 {
    let before_basecase = 0;
    if n == 0 { return 0; }
    return 10 + leave_arg({
        if n == 1 { return 100; }
        n - 1
    });
}
// A break in an argument can also abandon the pending addition by leaving
// a block around the return. The block's result then leaves the function.
recursive fn leave_block(n: i64) -> i64 {
    let before_basecase = 0;
    if n == 0 { return 0; }
    if n > 1 { return 10 + leave_block(n - 1); }
    let result = block {
        return 10 + leave_block({
            if n == 1 { break 100; }
            n - 1
        });
    };
    result
}
// Safe calls still become loop steps. The argument return must include
// their accumulated operations, but must exclude its own pending addition.
recursive fn leave_mixed(n: i64) -> i64 {
    let before_basecase = 0;
    if n == 0 { return 0; }
    if n > 1 { return 10 + leave_mixed(n - 1); }
    return 10 + leave_mixed({
        if n == 1 { return 100; }
        n - 1
    });
}
// The input of a for loop is evaluated before the loop catches breaks.
recursive fn leave_sequence(n: i64) -> i64 {
    let before_basecase = 0;
    if n == 0 { return 0; }
    if n > 1 { return 10 + leave_sequence(n - 1); }
    let result = block {
        return 10 + leave_sequence({
            for x in {
                if n == 1 { break 100; }
                n
            } {}
            n - 1
        });
    };
    result
}
// Early-out blocks do not capture continue. Safe calls in them still loop,
// including an argument break caught by its own block.
recursive fn safe_block(n: i64) -> i64 {
    let before_basecase = 0;
    block {
        if n <= 0 { break; }
        return 1 + safe_block(block {
            if n == 1 { break 0; }
            n - 1
        });
    }
    return 0;
}
recursive fn safe_plain_block(n: i64, k: i64) -> i64 {
    let before_basecase = 0;
    block {
        if n <= 0 { break; }
        return safe_plain_block(n - 1, k + 1);
    }
    return k;
}
fn main() {
    print(leave_arg(0));
    print(leave_arg(1));
    print(leave_arg(2));
    print(leave_arg(3));
    print(leave_block(0));
    print(leave_block(1));
    print(leave_block(2));
    print(leave_block(3));
    print(leave_mixed(0));
    print(leave_mixed(1));
    print(leave_mixed(2));
    print(leave_mixed(3));
    print(leave_sequence(0));
    print(leave_sequence(1));
    print(leave_sequence(2));
    print(leave_sequence(3));
    print(safe_block(4));
    print(safe_plain_block(4, 0));
}
