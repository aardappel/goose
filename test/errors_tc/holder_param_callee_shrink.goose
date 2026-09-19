// error: cannot call f, which shrinks xs while s is still used: it may hold a reference or slice into it
// f pops through the reference its by-value holder parameter carries, read
// out of id's result exactly: at the call that is a shrink of xs, which the
// reference points into, not of the holder, which f received a copy of
// (§5.1). s would read the popped slot that the push reuses.
struct H { r: i64[>..]& }
fn id(h: H) -> H { h }
fn f(v: H) { let r .= id(v).r; r.pop(); }
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    let s = xs[..];
    f(H { r: xs });
    xs.push(77);
    print(s[2], " ", xs.len);
}
