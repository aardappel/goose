// error: a relative reference must point within the same root as its location
// error: this reference is rooted at local
struct Node { id: i64, next: Node&<u32>? }
fn make() -> Node {
    let local = Node { id: 1 };
    Node { id: 2, next: local }
}
fn main() { let n = make(); print(n.id); }
