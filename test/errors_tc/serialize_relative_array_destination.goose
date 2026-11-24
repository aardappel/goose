// error: cannot construct Node[>..<] from Node[]: a shrink could remove an element
struct Node { next: Node&<u32>? }

fn main() {
    let image: u8[1] = [0];
    var nodes: Node[>..<] = from_bytes<Node[]>(image);
    print(nodes.len);
}
