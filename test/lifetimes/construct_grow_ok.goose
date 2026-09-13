// Growth of other arrays while a value is built in place (§1.3(4)):
// distinct locals, distinct parameter classes, a callee's own local, and
// the new contents of a whole assignment built from another array.
fn mk(b: i64[>..]&) -> i64[>..] { b.push(9); return [1, 2]; }
fn own() -> i64[>..] { var r: i64[>..] = []; r.push(7); r.push(8); return r; }
fn f(x: i64[>..]&, y: i64[>..]&) { x.append(mk(y)); }
fn tag(b: u8[][>..]&) -> u8[] { b.push("t"); return "u"; }
fn bump(b: i64[>..]&) -> i64[>..] {
    var r: i64[>..] = [];
    for v in b { r.push(v + 1); }
    return r;
}
fn main() {
    var a: i64[>..] = [];
    var b: i64[>..] = [];
    a.append(mk(b));
    print(a, " ", b);
    a.append(own());
    print(a);
    f(a, b);
    print(a, " ", b);
    var s: u8[][>..] = [];
    var t: u8[][>..] = [];
    s.push(tag(t));
    print(s.len, " ", t.len);
    a = bump(b);
    print(a);
}
