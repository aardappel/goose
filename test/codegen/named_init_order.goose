// runtime-debug: field evaluation and construction follow declaration order.
import std;
struct Pair { a: i64, b: i64 }
struct Triple { a: i64, b: i64, c: i64 }
struct Defaults { a: i64, b: i64 = 7, c: i64 }
struct Floats { a: f32, b: f32 }
struct Unsigned { a: u64, b: u64 }
enum Message { Text { head: u8[], flags: i64 = 0, tail: u8[] } }
var order = 0;
fn next() -> i64 { order++; order }
fn identity(x: i64) -> i64 { return x; }
fn pure(x: i64) -> i64 { let y = identity(x); if y > 0 { y } else { 0 } }
fn count(bytes: const u8[:]) -> i64 { bytes.len }
fn word() -> u8[] { return "hello"; }
fn main() {
    var x = 4;
    let p = Pair { b: pure(x), a: x };
    assert(p.a == 4 && p.b == 4);
    let t = Triple { c: p.b, a: p.a, b: if x > 0 { 2 } else { 3 } };
    assert(t.a == 4 && t.b == 2 && t.c == 4);
    let d = Defaults { c: x, a: x };
    assert(d.b == 7);
    let f = Floats { b: 2.0 * 3.0, a: 1.0 };
    assert(f.a == 1.0 && f.b == 6.0);
    let u: u64 = 18446744073709551615;
    let bits = Unsigned { b: u + 1, a: u << 1 };
    assert(bits.a == 18446744073709551614 && bits.b == 0);
    let m = Message.Text { tail: "world", head: word() };
    print(m);
    let counted = Pair { b: count([1, 2]), a: x };
    assert(counted.b == 2);
    // Reordering pure expressions does not reject an effect elsewhere.
    let ordered = Triple { a: next(), c: x, b: x };
    let same = Pair { a: next(), b: next() };
    assert(ordered.a == 1 && same.a == 2 && same.b == 3);
    print("named fields: declaration-order construction passed");
}
