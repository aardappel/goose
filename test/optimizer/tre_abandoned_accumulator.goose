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
    let result = block {
        return 10 + leave_block({
            if n == 1 { break 100; }
            n - 1
        });
    };
    result
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
}
