// error: function inner is in a recursive cycle and returns a reference, which needs an explicit result type
// A cycle's back edges take its functions' result roots before their
// returns are checked (§7.8): `inner` has inferred a reference result from
// its first return when its call back into the cycle joins it.
struct Ctx { args: i64[>..<]& }
recursive fn outer(c: Ctx&, n: i64) -> i64 {
    if n > 0 { return inner(c, n).len; }
    return 0;
}
fn inner(c: Ctx&, n: i64) {
    if n > 5 { return c.args; }
    c.args.push(outer(c, n - 1));
    return c.args;
}
fn main() {
    var s: i64[>..<] = [];
    var c = Ctx { args: s };
    print(outer(c, 3));
}
