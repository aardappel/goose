// error: c.args is not fixed-size and is not copied implicitly
// A function's final expression moves only the function's own locals
// (§4.1): into an inferred result, a branch giving what a reference
// parameter reaches is copied only by copy().
struct Ctx { args: i64[>..<]&, other: i64[>..<]& }
fn pick(c: Ctx&, k: bool) { if k { c.args } else { c.other } }
fn main() {
    var s: i64[>..<] = [1];
    var t: i64[>..<] = [2, 3];
    var c = Ctx { args: s, other: t };
    print(pick(c, true).len);
}
