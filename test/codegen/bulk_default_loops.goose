// runtime-debug all-levels
var calls = 0;
fn next() -> i64 { calls++; calls }
struct Effect { value: i64 = next() }
struct Flat { a: i64, b: f64, c: i32[3] }
fn fill<T>(s: T[:]) { for i in s.len { s[i] .= default<T>(); } }
fn grow<T>(a: T[>..]&, n: i64) { for i in a.len..n { a.push(default<T>()); } }
root struct Owner { a: Flat[>..] = [], b: i64[>..] = [] }
fn main() {
    var bytes: u8[>..] = [9, 8, 7];
    fill(bytes[1..]); assert(bytes[0] == 9 && bytes[1] == 0 && bytes[2] == 0);
    grow(bytes, 9); assert(bytes[0] == 9 && bytes[8] == 0 && bytes.len == 9);
    grow(bytes, -1); assert(bytes.len == 9);
    fill(bytes[0..0]);
    var owner = Owner {};
    grow(owner.a, 20);
    owner.a[2].a = 99;
    fill(owner.a[..]);
    assert(owner.a[2].a == 0 && owner.a[19].b == 0.0);
    var effects: Effect[>..] = [];
    grow(effects, 3); assert(calls == 3);
    fill(effects[..]); assert(calls == 6 && effects[0].value == 4 && effects[2].value == 6);
    var wide: i64[>..] = [123456789];
    let s = wide[..];
    for i in s.len { s[i] = default<i8>(); }
    assert(wide[0] == 0);
    print("bulk default loops: ok");
}
