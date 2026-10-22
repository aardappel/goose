// lsp: fixture for test/lsp_protocol.py: the index of the names as
// written, where the check rewrites a global's initializer in place (it
// puts in the defaults a call or a struct literal leaves out, and expands
// a const for), the arms a const for generates, and the literals and
// comments signature help counts no commas in. Lines and columns are
// 1-based.
struct P { a: i64 = 7, b: i64 }
enum Shape { Circle { r: i64 }, Rect { w: i64, h: i64 } }

fn g(a: i64) -> i64 { a }
fn f(x: i64 = g(1)) -> i64 { x }
fn scale(x: i64, by: i64 = 2) -> i64 { x * by }
fn take(p: P, k: i64 = 3) -> i64 { p.a + p.b + k }
fn one(x: i64) -> i64 { x }

let doubled = scale(21);
let y = f();
let v = take(P { b: 1 });
let shape: Shape = Shape.Rect { w: 1, h: 2 };
let n = match shape { const for <N, V> in variants<Shape> { V => one(2), } };

fn show<T>(value: T) {
    match value {
        const for <N, V> in variants<T> {
            V c => { const for <F, A> in fields<V> { print(F, " = ", c.A); } },
        }
    }
}

fn pick(c: u8, i: i64) -> i64 { i }

fn main() {
    let s: Shape = Shape.Circle { r: 1 };
    show(s);
    print(doubled + y + v + n + pick(',', 2) + pick('\'', /* , */ 3) + pick(
        // commas, as here, in a comment
        '"', 4));
}

// lsp: hover 11:15 "g(i64)"
// lsp: hints 7 0
// lsp: hints 11 1
// lsp: hint 11:17 "a:"
// lsp: hints 12 0
// lsp: hints 13 0
// lsp: hints 16 2
// lsp: hint 16:21 "x:"
// lsp: hint 18:14 "p:"
// lsp: hints 20 2
// lsp: hover 18:18 "b: i64"
// lsp: definition 18:18 -> 7:24
// lsp: rename 7:24 bb -> 3
// lsp: references 14:4 2
// lsp: definition 25:70 -> 25:15
// lsp: rename 25:15 d -> 2
// lsp: hover 25:15 "c: Shape.Circle"
// lsp: hover 25:15 "c: Shape.Rect"
// lsp: signature 35:43 "fn pick(c: u8, i: i64) -> i64" 1
// lsp: signature 35:67 "fn pick(c: u8, i: i64) -> i64" 1
// lsp: signature 37:14 "fn pick(c: u8, i: i64) -> i64" 1
