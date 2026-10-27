// error: S is not fixed-size and is not copied implicitly into the format overload taking it by value
// A format overload taking a T by value is given a copy (§3.7), which a
// resizable T is never given implicitly (§4.1): it takes such a T by
// reference.
struct S { tag: i64, xs: i64[>..] }
fn format(out: u8[>..]&, v: S) { format(out, v.tag); }
fn main() {
    var s = S { tag: 7, xs: [] };
    print(s);
}
