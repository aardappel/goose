// The container came from a caller, so what a reference read out of it points
// at may be storage this function cannot see (§9.5): the root bounds the
// lifetime but is not an identity, and the relative store needs one (§3.9).
struct Node { key: i32, plain: Node&?, next: Node&<u32>? }
fn relink(pool: Node[>..]&) {
    let n = pool[0].plain;
    assert(n);
    pool[1].next .= n;
}
fn main() {
    var pool: Node[>..] = [];
    pool.push(Node { key: 1 });
    pool.push(Node { key: 2 });
    pool[0].plain .= pool[1];
    relink(pool);
}
