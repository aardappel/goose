// error: B is not fixed-size and is not copied implicitly into the format overload taking it by value
// A format overload taking a T by value is given a copy (§3.7), which a
// variable-size T is never given implicitly (§4.1): it takes such a T by
// reference, as writable as the T's storage is.
struct B { tag: i64, xs: i64[] }
fn format(out: u8[>..]&, v: B) {
    v.xs[0] = 99;
    format(out, v.tag, ":", v.xs);
}
fn main() {
    let b = B { tag: 7, xs: [1, 2] };
    print(b);
}
