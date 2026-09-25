// error: returning a reference rooted in t, which dies with this function
// error: the result type i64[>..<]& is inferred from an earlier return
// The first return makes an inferred result a reference (§7.1), which the
// function's own local, moved by a value result, cannot be.
struct Ctx { args: i64[>..<]& }
fn pick(c: Ctx&, k: bool) {
    if k { return c.args; }
    var t: i64[>..<] = [2];
    return t;
}
fn main() {
    var s: i64[>..<] = [1];
    var c = Ctx { args: s };
    print(pick(c, true));
}
