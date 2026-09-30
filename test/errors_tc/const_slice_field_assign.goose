// error: cannot assign through this path
// The elements a slice field of a `const` value views are as writable as the
// field's type says (§9.5), but the field itself is the const value's
// contents: it is not re-sliced.
struct HS { s: i64[:] }
fn main() {
    var a = [1, 2, 3];
    const hs = HS { s: a[..] };
    hs.s[0] = 7;
    hs.s = a[1..];
    print(a);
}
