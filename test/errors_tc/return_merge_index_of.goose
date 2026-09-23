// error: .index_of needs a reference rooted at the array itself
// error: this one's root is not known exactly, only that it outlives p
// A result whose returns give two roots is exact at neither (§9.2): `pick`
// may return an element of q, so it is no index into p (§3.3), although
// the roots are merged at p here.
struct Node { v: i64 }

fn pick(a: Node[>..]&, b: Node[>..]&, k: bool) -> Node& {
    if k { return a[0]; }
    return b[0];
}

fn main() {
    var p: Node[>..] = [];
    var q: Node[>..] = [];
    p.push(Node { v: 1 });
    q.push(Node { v: 2 });
    let n: Node& = pick(p, q, true);
    print(p.index_of(n));
}
