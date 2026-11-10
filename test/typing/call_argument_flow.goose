// An argument is checked more than once -- for its type, then against its
// parameter, and a call among the arguments once more per call around it --
// and what it does to the flow counts once (§4.4, §7.1): a `let` a block
// assigns, wherever the call taking the block stands among the arguments of
// another call. The body runs after the arguments, so a block of the call
// reads what an argument assigned.
import slot_pool;

struct Node { v: i64 }
enum Shape { Circle { r: i64 }, Square { s: i64 } }
var pool = slot_pool<Node> {};

fn id<T>(v: T) -> T { v }
fn add(a: i64, b: i64) -> i64 { a + b }
fn once<F>() -> i64 { F(); return 1; }
fn with<F>(v: i64) -> i64 { return F(v); }
fn mk<F>() -> Shape { F(); return Shape.Circle { r: 2 }; }
fn area(c: Shape.Circle, k: i64) -> i64 { c.r * k }
fn area(s: Shape.Square, k: i64) -> i64 { s.s * k }
fn sum(xs: i64[:]) -> i64 { var t = 0; for x in xs { t += x; } return t; }
fn first(a: i64[>..]&) -> i64 { a[0] }
fn text<F>() -> u8[4] { F(); return "abcd"; }
fn nums<F>() -> i64[>..] { F(); var r: i64[>..] = [1, 2]; return r; }

fn nested() {
    let x: i64;
    print(id(once() { x = 1; }));
    let y: i64;
    print(id(id(once() { y = 2; })));
    let z: i64;
    print(add(once() { z = 3; }, with(4) { it + z }));
    print(x, " ", y, " ", z);
}

fn receivers() {
    let x: i64;
    print(once() { x = 1; }.add(2));
    let y: i64;
    let s = once() { y = 2; }.str();
    let z: i64;
    print(nums() { z = 3; }.bytes_of().len);
    print(s, " ", x, " ", y, " ", z);
}

fn blocks() {
    let x: i64;
    let r = with(4) { n => add(once() { x = n; }, n) };
    let y: i64;
    print(with(once() { y = 2; }) { it + 1 });
    print(r, " ", x, " ", y);
}

fn dispatched() {
    let x: i64;
    let y: i64;
    print(area(mk() { x = 1; }, once() { y = x + 1; }));
    let z: i64;
    let sq: Shape = Shape.Square { s: 2 };
    print(area(sq, once() { z = 3; }));
    print(x, " ", y, " ", z);
}

fn constructs() {
    var a: i64[>..] = [1, 2];
    var b: i64[>..] = [3, 4];
    let x: i64;
    print(first(if id(once() { x = 1; }) > 0 { a } else { b }));
    var c = [1, 2];
    var d = [3, 4, 5];
    let y: i64;
    print(sum(if id(once() { y = 2; }) > 0 { c } else { d }));
    let z: i64;
    if id(once() { z = 3; }) > 0 { print(x, " ", y, " ", z); }
}

fn adapted() {
    var word: u8[>..] = "ab";
    let x: i64;
    print(text() { x = 1; } == word[..]);
    let y: i64;
    var out = [];
    out = nums() { y = 2; };
    let z: i64;
    var more = [];
    more.push(once() { z = 3; });
    let w: i64;
    var all = [];
    all.append(nums() { w = 4; });
    print(out.len, " ", more.len, " ", all.len, " ", x, " ", y, " ", z, " ", w);
}

fn owned() {
    let h = pool.take(Node { v: 7 });
    print(id(once() { pool.release(h); }));
    let g = pool.take(Node { v: 8 });
    pool.release(g) { print(it.v); };
    print(pool.free_slots.len);
}

fn main() {
    nested();
    receivers();
    blocks();
    dispatched();
    constructs();
    adapted();
    owned();
}
