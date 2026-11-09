// error: field b of struct Holder has type own Buf&, an own value with no empty state
// An own field is read with take(), which leaves it empty, so its type needs
// an empty value: an optional reference or a slice.
struct Buf { n: i64 }
struct Holder { b: own Buf& }
fn main() {}
