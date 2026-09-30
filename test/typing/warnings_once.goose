// warning: redundant &: ga is passed by reference without it
// warning: redundant &: a is passed by reference without it
// warning: redundant &: a is passed by reference without it
// warning: redundant &: a is passed by reference without it
// warning: redundant &: the construct's value is a copy of a either way
// warning: redundant &: a is passed by reference without it
// warning: redundant &: a is passed by reference without it
// warning: redundant &: a is passed by reference without it
// warning: redundant &: t binds by reference without it
// warning: redundant &: a is passed by reference without it
// warning: redundant &: a is passed by reference without it
// A node can be checked more than once: a call checks its arguments
// bottom-up and again against the chosen parameter types, resolving each
// call nested in them again; a builtin rendering a UFCS receiver checks it
// after its call did; tag dispatch checks again the argument it binds by
// reference; the builtin `format` checks the arguments a user overload set
// of that name failed to take; a default and a function value's body are
// cloned anew at each check of their call, and a function's body per
// specialization; and a cycle's body is checked in every round. Each
// warning above prints once for the `&` as written, however deep the calls
// nest, and the two `&`s of the last line warn once each.
var ga = 1;

struct Txt { body: u8[] }
struct Pt { x: i64 }
enum Shape { Circle { r: i64 }, Point { k: i64 } }

fn g(v: i64&) -> i64 { v }
fn inc(x: i64) -> i64 { x + 1 }
fn dflt(x: i64 = g(&ga)) -> i64 { x }
fn apply<F>() -> i64 { inc(inc(F())) }
fn gen(x) -> i64 {
    var a = 2;
    print(x);
    g(&a)
}
recursive fn down(n: i64) -> i64 {
    var a = 1;
    if n > 0 { down(n - 1) + g(&a) } else { 0 }
}
fn area(s: Shape.Circle&, n: i64) -> i64 { s.r * n }
fn area(s: Shape.Point&, n: i64) -> i64 { s.k + n }
fn format(out: u8[>..]&, p: Pt) { out.append("pt"); }

fn main() {
    var a = 1;
    var b = 2;
    let c = a < b;
    print(inc(dflt()), " ", inc(inc(dflt())));
    print(gen(3), " ", gen(true));
    print(down(3));
    print(inc(inc(inc(if c { g(&a) } else { b }))));
    print((if c { &a } else { b }).str());
    print(apply() { g(&a) });
    var s: Shape.. = Shape.Circle { r: 3 };
    var p: Shape.. = Shape.Point { k: 4 };
    print(area(if g(&a) > 0 { s } else { p }, 2));
    var buf: u8[>..] = [];
    format(buf, g(&a), " ", Pt { x: 1 });
    print(buf);
    let t = Txt { body: "hey" };
    print(inc(inc(block { let r = &t; break r.body.len; })));
    print(inc(g(&a) + g(&a)));
}
