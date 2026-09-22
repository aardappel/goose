struct Node { value: i64, next: Node&<u8>? }
struct Self { link: Self&<u8>, value: i64 }
fn main() {
    var nodes: Node[>..] = [];
    nodes.push(Node { value: 42 });
    assert(nodes[0].next == null);
    nodes[0].next .= nodes[0];
    let next .= nodes[0].next;
    if next { print(next.value); }
    nodes[0].next .= null;
    assert(nodes[0].next == null);
    let s = Self { link: self, value: 99 };
    print(s.link.value);
}
