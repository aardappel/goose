// error: function inner is in a recursive cycle and returns a reference, which needs an explicit result type
// A function already in a recursive cycle, by a call back into it, may not
// infer a reference result from a return after that call either (§7.8).
struct Ctx { args: i64[>..<]& }
recursive fn outer(c: Ctx&, n: i64) -> i64 {
    if n > 0 { return inner(c, n).len; }
    return 0;
}
fn inner(c: Ctx&, n: i64) {
    c.args.push(outer(c, n - 1));
    return c.args;
}
fn main() {
    var s: i64[>..<] = [];
    var c = Ctx { args: s };
    print(outer(c, 3));
}
