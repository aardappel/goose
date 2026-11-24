// error: cannot construct Node[>..<] from Node[>..]: a shrink could remove an element
struct Node { next: Node&<u32>? }

fn decode(image: u8[:]) -> Node[>..] {
    let nodes = from_bytes<Node[>..]>(image);
    nodes
}

recursive fn read_at(image: u8[:], depth: i64) -> Node[>..] {
    fn finish() { return decode(image) from read_at; }
    if depth == 0 { finish(); }
    read_at(image, depth - 1)
}

fn main() {
    let image: u8[1] = [0];
    let nodes: Node[>..<] = read_at(image, 1);
    print(nodes.len);
}
