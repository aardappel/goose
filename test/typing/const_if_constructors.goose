// const if in constructor initializer lists (const_for.md §11.4): entries
// chosen per field, nothing contributed, positional and named entries, and
// a variant constructor.
struct Item { name: u8[..8], price: i64, tax: i64 = 20, discount: i64 = 0 }
struct Quad { a: i64, b: i64, c: i64, d: i64 }
enum Shape { Circle { r: i64 = 1, filled: bool }, Dot }
let WIDE = true;

// A copy of src with every field that declares a default reset to it.
fn reset_defaults<T>(src: T&) -> T {
    T { const for <N, A> in fields<T> { const if !has_default<A> { A: src.A, } }, .. }
}
fn main() {
    var item = Item { name: "pen", price: 5, tax: 7, discount: 3 };
    print(reset_defaults(item));
    print(Quad { const if WIDE { 1, 2 } else { 3, 4 }, const if !WIDE { 9, 9 } else const if WIDE { 5, 6 } });
    print(Quad { 1, const if !WIDE { 7, }, 2, 3, 4 });
    print(Item { name: "cap", const if WIDE { price: 1, } else { price: 2, tax: 3, }, .. });
    let c: Shape = Shape.Circle {
        const for <N, A> in fields<Shape.Circle> { const if !has_default<A> { A: true, } },
        ..
    };
    print(c);
}
