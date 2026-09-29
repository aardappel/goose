// runtime-debug: a body inlined into its caller passes its parameter on, so the callee may shrink the caller's own array.
recursive fn cut(p: i64[>..<]&) { p.pop(); p.pop(); }
fn outer(p: i64[>..<]&) { cut(p); }
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    let n = a.len;
    outer(a);
    if n > 0 { print(a[0]); } // bce:keep
    if n > 2 { print(a[2]); } // bce:keep
}
