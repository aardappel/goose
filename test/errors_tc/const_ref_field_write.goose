// error: cannot assign through this path
// A reference read out of a field is as writable as the field's type says,
// whatever the path to it (§9.5): a `const T&` field reads back read-only
// even out of a `var`, and so does the path going on through it.
struct S { n: i64 }
struct H { r: const S& }
fn main() {
    var x = S { n: 1 };
    var h = H { r: x };
    h.r.n = 7;
    print(x.n);
}
