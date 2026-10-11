// error: cannot use o.inner in the value assigned to o.s
// A field referring to a value of the type holding the array assigned may
// refer to that value itself (§4.4).
struct S { n: i64, items: i64[>..] }
struct O { k: i64, inner: S&, s: S }
fn mk(s: S&) -> S { return S { n: s.n, items: copy(s.items) }; }
fn run(o: O&) { o.s = mk(o.inner); }
fn main() {
    var x = S { n: 1, items: [1] };
    var o = O { k: 1, inner: x, s: S { n: 2, items: [2] } };
    o.inner .= o.s;
    run(o);
    print(o.s.items);
}
