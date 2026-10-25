// An earlier by-value argument keeps its value when a later argument
// changes the parameter. Keep base-case inlining out of this test.
recursive fn keep_first(var x: i64, n: i64) -> i64 {
    let ignored = 0;
    if n == 0 { return x; }
    return keep_first(x, { x += 1; n - 1 });
}

fn main() {
    print(keep_first(7, 0));
    print(keep_first(7, 3));
}
