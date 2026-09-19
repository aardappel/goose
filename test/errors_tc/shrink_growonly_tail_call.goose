// error: cannot call drop, which shrinks st while s is still used: it may hold a reference or slice into it
// drop shrinks the grow-only tail of st: the call takes the §5.1 scan, as for
// a grow-only array variable (§5.1).
struct S { n: i64, t: i64[>..] }
fn drop(xs: i64[>..]&) { xs.pop(); }
fn main() {
    var st = S { n: 1, t: [1, 2, 3] };
    let s = st.t[..];
    drop(st.t);
    st.t.push(77);
    print(s[2]);
}
