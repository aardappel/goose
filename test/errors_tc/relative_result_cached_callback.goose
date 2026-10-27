// error: a relative reference must point within the same root as its location
// error: call of both links a target rooted at a to receiving storage b
struct Node { id: i64, next: Node&<u32>? }
fn append<F>(xs: Node[>..]&) { xs.push(F()); }
fn both<F>(a: Node[>..]&, b: Node[>..]&) { append(a, F); append(b, F); }
fn main() {
    var a: Node[>..] = [Node { id: 1 }];
    var b: Node[>..] = [];
    let first .= a[0];
    both(a, b) { Node { id: 2, next: first } };
}
