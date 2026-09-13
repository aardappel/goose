// runtime-debug: a frame object's nested tail shares the global owner's stack.
struct Bag { tag: i64, items: i64[>..] }
struct Outer { id: i64, bag: Bag }
var global: Outer = Outer { id: 1, bag: Bag { tag: 2, items: [] } };

// Keep these calls across optimization so both spellings of the same stack
// remain inside a callee with a fat-reference parameter.
recursive fn fill_array(xs: i64[>..]&, n: i64) {
    if n > 0 { fill_array(xs, n - 1); return; }
    xs.push(10);
    global.bag.items.push(20);
    xs.push(30);
}
recursive fn fill_bag(b: Bag&, n: i64) {
    if n > 0 { fill_bag(b, n - 1); return; }
    b.items.push(40);
    global.bag.items.push(50);
    b.items.push(60);
}
fn main() {
    for i in 2 {
        fill_array(global.bag.items, i);
        fill_bag(global.bag, i);
    }
    print(global.bag.items.len);
    for x in global.bag.items { print(x); }
}
