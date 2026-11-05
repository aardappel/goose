// const if in statement lists (const_for.md §11): has_default over structs,
// variant payloads and generic structs; unchosen branches left unchecked;
// chains; flow through the chosen block; nesting with const for.
struct Item { name: u8[..32], price: f64, tax: f64 = 0.2, discount: f64 = 0.0 }
struct Inner { a: i64 = 1, b: i64 = 2 }
struct Outer { inner: Inner, count: i64 = 5, flag: bool }
struct Pair<T> { first: T, second: T = default<T>() }
struct Packed { n: i64, count: varint = 1 }
struct Config { port: i64, retries: i64 = 3, level: i64 }
enum Shape { Circle { r: i64 = 1 }, Rect { w: i64, h: i64 }, Dot }
let TRACE = false;
const LIMIT = 4;

fn required<T>() {
    const for <N, A> in fields<T> { const if !has_default<A> { print("required ", N); } }
}
fn defaulted<T>() -> i64 {
    var n = 0;
    const for <N, A> in fields<T> { const if has_default<A> { n++; } }
    return n;
}
// A variant payload's fields answer the same way.
fn payload_defaults(s: Shape) {
    match s {
        const for <N, V> in variants<Shape> {
            V v => {
                const for <F, A> in fields<V> {
                    const if has_default<A> { print(N, ".", F, " defaults"); }
                    else { print(N, ".", F, " required"); }
                }
            },
        }
    }
}
// The branch a field does not take is never checked: the varint write
// where the field has a default.
fn bump_required<T>(v: T&) {
    const for <N, A> in fields<T> {
        const if !has_default<A> { v.A = v.A + 1; print(N, " ", v.A); }
        else { print(N, " kept"); }
    }
}
fn flow(n: i64) -> i64 {
    var x: i64;
    const if LIMIT == 4 { x = n * 2; } else { x = n; }
    var i = 0;
    var seen = 0;
    while i < 10 {
        i++;
        const if TRACE { print("i ", i); }
        const if LIMIT == 4 { if i > 3 { break; } } else { if i > 5 { break; } }
        const if true { if i == 2 { continue; } }
        seen++;
    }
    const if LIMIT > 100 { return -1; }
    return x + seen;
}
fn scoped() {
    const if LIMIT == 4 {
        let local = 1;
        fn inner() -> i64 { local + 1 }
        print(inner());
    }
    const if LIMIT == 4 { let local = 7; print(local); }
}
fn describe<T>(v: T) {
    const if same<T, i64> { print("int ", v); }
    else const if same<T, bool> { print("bool ", v); }
    else { print("other ", v); }
}
fn nested() {
    const if LIMIT == 4 {
        const for <C> in [1, 2, 3] {
            const if C != 2 { print("c ", C); }
        }
    }
}
fn main() {
    required<Item>();
    print(defaulted<Item>());
    required<Outer>();
    print(defaulted<Outer>());
    required<Pair<i64>>();
    print(defaulted<Pair<bool>>());
    payload_defaults(Shape.Circle { r: 3 });
    payload_defaults(Shape.Rect { w: 1, h: 2 });
    payload_defaults(Shape.Dot);
    var packed = Packed { 10, 20 };
    bump_required(packed);
    var config = Config { port: 80, level: 2, .. };
    bump_required(config);
    print(flow(5));
    scoped();
    describe(1);
    describe(true);
    describe(2.5);
    nested();
    const if false { unavailable(); }
    const if TRACE { unavailable(); } else { print("trace off"); }
    const if !TRACE { print("quiet"); }
    const if LIMIT > 3 { print("limit ", LIMIT); } else const if LIMIT > 1 { print("mid"); } else { print("low"); }
    const if LIMIT > 100 { print("never"); }
}
