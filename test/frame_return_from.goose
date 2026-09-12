// runtime-debug: long-distance returns carry a whole resizable frame object.
struct Bag { tag: i64, items: i64[>..] }
struct Nested { id: i64, bag: Bag }

fn leave(n: i64) {
    return Nested { id: n, bag: Bag { tag: n + 10, items: [n, n + 1] } } from build;
}
fn intermediate(n: i64) {
    var scratch: i64[>..] = [101, 102];
    scratch.push(103);
    leave(n);
    print(999);
}
fn build(n: i64) -> Nested {
    var scratch: i64[>..] = [201, 202];
    if n > 0 { intermediate(n); }
    return Nested { id: 0, bag: Bag { tag: 5, items: [8] } };
}
fn leave_pair() {
    return Bag { tag: 70, items: [71, 72] }, 80 from build_pair;
}
fn build_pair() -> Bag, i64 {
    leave_pair();
    return Bag { tag: 0, items: [] }, 0;
}
fn invoke<F>() { F(); }
fn from_block(n: i64) -> Bag {
    invoke() { return Bag { tag: n, items: [n + 1] }; }
    return Bag { tag: 0, items: [] };
}
fn main() {
    for i in 3 {
        var out = build(i);
        out.bag.items.push(90 + i);
        print(out.id, out.bag.tag, out.bag.items.len, out.bag.items[0],
              out.bag.items[out.bag.items.len - 1]);
    }
    let b, n = build_pair();
    print(b.tag, b.items.len, b.items[1], n);
    var b2 = from_block(30);
    b2.items.push(32);
    print(b2.tag, b2.items.len, b2.items[0], b2.items[1]);
}
