// error: storing a reference rooted at local, which does not outlive the destination
// The loop's next iteration runs the function value with cur holding a link
// into the local array, which may not go into the caller's pool (§7.6,
// §9.2).
struct Node { v: i64, first: Node? }
struct Cur { first: Node?, count: i64 }

fn twice<F>() { F(0); F(1); }

fn build(pool: Node[>..]&) -> i64 {
    var local: Node[>..] = [];
    var cur = Cur { first: null, count: 0 };
    cur.first .= pool.push(Node { v: 1, first: null });
    var total = 0;
    for i in 3 {
        twice() { r => total += pool.push(Node { v: r, first: cur.first }).v; };
        cur.first .= local.push(Node { v: 5, first: null });
    }
    return total;
}

fn main() {
    var pool: Node[>..] = [];
    print(build(pool));
}
