// error: a relative reference must point within the same root as its location
// error: call of make links a target rooted at a to receiving storage b
struct Node { id: i64, next: Node&<u32>? }
fn make(first: Node&) -> Node { Node { id: 2, next: first } }
fn main() {
    var a: Node[>..] = [Node { id: 1 }];
    var b: Node[>..] = [];
    a.push(make(a[0]));
    b.push(make(a[0]));
}
