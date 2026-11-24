// error: retained storage may have a self-relative link to a removed element
struct Node { next: Node&<u32>? }

fn trim(nodes: Node[>..]&) {
    if nodes.len > 1 { nodes.resize(1); }
}

fn main() {
    let image: u8[1] = [0];
    var nodes, ok = from_bytes<Node[>..]>(image);
    trim(nodes);
    print(ok, nodes.len);
}
