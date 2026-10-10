// error: storing a reference rooted at local, which does not outlive the destination
// The nested function is checked again for its second call, where cur holds
// a link into the local array, which may not go into the caller's pool
// (§7.5, §9.2).
struct Node { v: i64, first: Node? }
struct Cur { first: Node?, count: i64 }

fn build(pool: Node[>..]&) -> i64 {
    var local: Node[>..] = [];
    var cur = Cur { first: null, count: 0 };
    cur.first .= pool.push(Node { v: 1, first: null });
    fn close() -> Node& { return pool.push(Node { v: 2, first: cur.first }); }
    let n .= close();
    cur.first .= local.push(Node { v: 5, first: null });
    let m .= close();
    return pool.len + n.v + m.v;
}

fn main() {
    var pool: Node[>..] = [];
    print(build(pool));
}
