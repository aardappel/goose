// error: expected a value of type i64[:], got u8[2]
// A generic struct literal infers T from its first slice field's array; the
// second then has to be an array of that T (§3.10, §7.1).
struct V<T> { s: T[:], t: T[:] }
fn main() {
    var a: i64[2] = [1, 2];
    var b: u8[2] = [3, 4];
    let v = V { s: a, t: b };
    print(v.s);
}
