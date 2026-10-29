// runtime-debug all-levels
import slot_pool;
fn give<F>() -> i64 { F() }
fn nested<F>() -> i64 { give() { F() } }
recursive fn digits(d: i64) -> i64 {
    if d == 1 { return give() { d }; }
    return digits(d - 1) * 10 + d;
}
recursive fn constant(d: i64) -> i64 {
    if d == 1 { return give() { 1 }; }
    return constant(d - 1) * 10 + d;
}
recursive fn written(var d: i64) -> i64 {
    if d == 1 { return give() { d += 10; d }; }
    return written(d - 1) * 10 + d;
}
recursive fn references(d: i64, value: i64&) -> i64 {
    if d == 1 { return nested() { value + d }; }
    return references(d - 1, value) * 10 + d;
}
recursive fn local_function(d: i64) -> i64 {
    fn read() -> i64 { d }
    if d == 1 { return read(); }
    return local_function(d - 1) * 10 + d;
}
recursive fn nonlocal(d: i64) -> i64 {
    if d == 1 { return give() { return d from nonlocal; 0 }; }
    return nonlocal(d - 1) * 10 + d;
}
struct Node { depth: i64, left: Node&<u32>?, right: Node&<u32>? }
recursive fn build(p: slot_pool<Node>&, depth: i64) -> Node& {
    if depth == 1 { return p.alloc_with() { Node { depth, null, null } }; }
    let l .= build(p, depth - 1);
    let r .= build(p, depth - 1);
    return p.alloc_with() { Node { depth, l, r } };
}
recursive fn sum(n: Node&) -> i64 {
    var total = n.depth;
    let l = n.left; let r = n.right;
    if l { total += sum(l); }
    if r { total += sum(r); }
    return total;
}
var global = slot_pool<Node> {};
fn main() {
    var pool = slot_pool<Node> {};
    var value = 1;
    assert(digits(3) == 123);
    assert(constant(3) == 123 && written(3) == 1123);
    assert(references(3, value) == 223);
    assert(local_function(3) == 123 && nonlocal(3) == 123);
    assert(sum(build(pool, 4)) == 26);
    assert(sum(build(global, 3)) == 11);
    print("recursive base-case captures: ok");
}
