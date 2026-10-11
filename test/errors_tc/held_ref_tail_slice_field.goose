// error: cannot call f, which shrinks t while t is still used: a reference into it was stored there
// A slice field viewing the struct's own tail (§5.1).
struct T { s: const u8[:], r: u8[>..] }
fn f(t: T&) { t.r.clear(); }
fn main() {
    var t = T { s: "", r: [1, 2] };
    t.s = t.r[..];
    f(t);
    print(t.s);
}
