// A reference variable bound only inside a loop body: a use earlier in the
// body than the rebind sees the root the rebind gives it, since the body is
// checked again with what its first pass fed back to the loop's head (§9.2).
// Two node pools are in scope, so the pointee type alone could not have said
// which one.
struct Node { v: i32, next: Node&<u32>? }

fn main() {
    var pool: Node[>..] = [];
    var other: Node[>..] = [];
    other.push(Node { v: 100 });
    var last: Node? = null;
    for i in 4 {
        let child .= pool.push(Node { v: i as i32 });
        if last { last.next .= child; }  // a relative link needs last's root exactly
        last .= child;
    }
    var total = 0;
    var cur: Node? = pool[0];
    while cur { total += cur.v; cur .= cur.next; }
    print(total, " ", other[0].v);
}
