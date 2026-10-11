// error: cannot use e in the value assigned to o.s: e may refer to o.s
// The same with the reference bound to a variable first (§4.4).
struct S { n: i64, items: i64[>..] }
struct O { k: i64, inner: S&, s: S }
fn mk(s: S&) -> S { return S { n: s.n, items: copy(s.items) }; }
fn run(o: O&) {
    let e: S& = o.inner;
    o.s = mk(e);
}
fn main() {
    var x = S { n: 1, items: [1] };
    var o = O { k: 1, inner: x, s: S { n: 2, items: [2] } };
    run(o);
    print(o.s.items);
}
