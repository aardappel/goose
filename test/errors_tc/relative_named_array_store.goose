// error: cannot construct Node[>..<] from Node[]: a shrink could remove an element
struct Node { next: Node&<u32>? }

fn link(nodes: Node[]&) { nodes[0].next .= nodes[1]; }

fn linked() -> Node[] {
    var nodes: Node[] = [Node {}, Node {}];
    link(nodes);
    return nodes;
}

fn main() {
    var nodes: Node[>..<] = linked();
    print(nodes.len);
}
