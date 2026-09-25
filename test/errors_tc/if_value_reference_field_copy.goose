// error: c.args is not fixed-size and is not copied implicitly
// Branches giving references to non-fixed arrays are copied as their
// pointees where the if's destination names no type (§3.8, §4.1): the push
// would land in a copy. `let a = c.args;` alone binds the reference, but a
// construct's value is no variable binding.
struct Ctx { args: i64[>..<]&, other: i64[>..<]& }
fn f(c: Ctx&, k: bool) {
    let a = if k { c.args } else { c.other };
    a.push(1);
}
fn main() {
    var s: i64[>..<] = [];
    var t: i64[>..<] = [];
    var c = Ctx { args: s, other: t };
    f(c, s.len == 0);
    print(s.len, " ", t.len);
}
