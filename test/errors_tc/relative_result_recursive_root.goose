// error: a relative reference must point within the same root as its location
// error: call of make links a target rooted at b to receiving storage a
struct Node { id: i64, next: Node&<u32>? }
recursive fn make(a: Node&, b: Node&, n: i64) -> Node {
    if n == 0 { return Node { id: 2, next: a }; }
    make(b, a, n - 1)
}
fn main() {
    var a: Node[>..] = [Node { id: 1 }];
    var b: Node[>..] = [Node { id: 3 }];
    a.push(make(a[0], b[0], 1));
}
