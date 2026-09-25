// error: pick(o.c.get(), c) is not fixed-size and is not copied implicitly (§4.1): pass copy(pick(o.c.get(), c)) for a copy
// A diagnostic names an expression as written: an argument or a receiver
// that binds by reference implicitly prints without an `&` (§4.1).
struct Ctx { args: i64[>..<]& }
struct Outer { c: Ctx }
fn get(c: Ctx&) -> i64[>..<]& { return c.args; }
fn pick(a: i64[>..<]&, c: Ctx&) -> i64[>..<]& { return a; }
fn main() {
    var s: i64[>..<] = [1];
    var o = Outer { c: Ctx { args: s } };
    var c = Ctx { args: s };
    let v: i64[>..<] = pick(o.c.get(), c);
}
