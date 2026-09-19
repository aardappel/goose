// error: a relative reference must point within the same root as its location
// error: it was read out of keep and may point into the caller's storage behind pool
// A reference kept in a local array after it was read out of a parameter's
// pointee may point wherever the caller's links lead, here into other: the
// parameter's class only bounds it, so it is no identity for the relative
// store, although pool's class is the one candidate that can hold a Node
// (§9.5, §3.9).
struct Node { key: i32, plain: Node&?, next: Node&<u32>? }
fn relink(pool: Node[>..]&) {
    var keep: (Node&?)[>..] = [];
    keep.push(pool[0].plain);
    let n = keep[0];
    assert(n);
    pool[1].next .= n;
}
fn main() {
    var other: Node[>..] = [];
    other.push(Node { key: 3 });
    var pool: Node[>..] = [];
    pool.push(Node { key: 1 });
    pool.push(Node { key: 2 });
    pool[0].plain .= other[0];
    relink(pool);
}
