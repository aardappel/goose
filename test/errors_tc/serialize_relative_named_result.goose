// error: cannot construct Node[>..<] from Node[>..]: a shrink could remove an element
struct Node { next: Node&<u32>? }

fn decode(image: u8[:]) -> Node[>..] {
    let nodes = from_bytes<Node[>..]>(image);
    return nodes;
}

fn main() {
    let image: u8[1] = [0];
    let nodes: Node[>..<] = decode(image);
    print(nodes.len);
}
