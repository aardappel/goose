// error: a relative reference must point within the same root as its location
// error: it was read out of h and may point into pool or spare
// h.r is rebound to a node of spare where spare is not empty, so the link
// may leave pool (§3.9, §9.5).
struct Node { key: i32, next: Node&<u32>? }
struct H { r: Node& }
fn main() {
    var pool: Node[>..] = [Node { key: 1 }, Node { key: 2 }];
    var spare: Node[>..] = [Node { key: 9 }];
    var h = H { r: pool[0] };
    if spare.len > 0 { h.r .= spare[0]; }
    pool[1].next .= h.r;
    let n = pool[1].next;
    if n { print(n.key); }
}
