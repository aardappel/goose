// error: cannot call wipe, which shrinks reusable pool p: its slots stay live for the freelist (§5.4)
// Assigning a grow-only array whole through a reference is a shrink, which a
// reusable pool never takes.
struct Node { v: i64 }
fn wipe(a: Node[>..]&) { a = []; }
fn main() {
    reusable var p: Node[>..] = [];
    p.alloc_index(Node { v: 1 });
    wipe(p);
    print(p.len);
}
