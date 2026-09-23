// error: cannot call inner, which shrinks s: an earlier expression value
// error: may still refer into s
// A mark is a `let` of the activation that resizes (§5.2): outer's mark is
// shorter than s is when inner starts, so inner frees the element the held
// slice views.
fn first(v: i64[:], unused: i64) -> i64 { v[0] }
fn outer(s: i64[>..<]&) -> i64 {
    let mark = s.len;
    fn inner() -> i64 {
        s.resize(mark);
        0
    }
    s.push(5);
    first(s[mark..], inner())
}
fn main() {
    var s: i64[>..<] = [];
    print(outer(s));
}
