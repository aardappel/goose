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
fn main() {
    print(leave_arg(0));
    print(leave_arg(1));
    print(leave_arg(2));
    print(leave_arg(3));
}
