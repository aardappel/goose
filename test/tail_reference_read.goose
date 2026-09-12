import std;
fn change(p: i64&) { p = 10; }
recursive fn sum(p: i64&, n: i64) -> i64 {
    // Keep base-case inlining from obscuring the independent TRE path.
    let ignored = 0;
    if n == 0 { change(p); return 0; }
    return sum(p, n - 1) + p;
}
fn setbits(p: u64&) { p = 8; }
recursive fn bits(p: u64&, n: i64) -> u64 {
    let ignored = 0;
    if n == 0 { setbits(p); return 0; }
    return bits(p, n - 1) | p;
}
// An ordinary by-value scalar can still be carried by an accumulator.
recursive fn scalar(p: i64, n: i64) -> i64 {
    let ignored = 0;
    if n == 0 { return 0; }
    return scalar(p, n - 1) + p;
}
fn main() {
    var x = 1;
    print(sum(x, 2), " ", x);
    var b: u64 = 1;
    print(bits(b, 2), " ", b);
    print(scalar(3, 4));
}
