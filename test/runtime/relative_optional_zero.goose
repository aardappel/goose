struct Node { next: Node&<u8>?, value: i64 }
fn main() {
    var nodes: Node[>..] = [];
    nodes.push(Node { value: 42 });
    nodes[0].next .= nodes[0];
    print("must not lose the target");
}
