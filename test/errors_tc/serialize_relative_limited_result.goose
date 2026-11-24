// error: a self-relative link may name a slot beyond the live length
struct Node { next: Node&<u32>? }

fn decode(image: u8[:]) -> Node[] {
    let nodes = from_bytes<Node[]>(image);
    nodes
}

fn shorten(image: u8[:]) -> Node[..] {
    var nodes: Node[..] = decode(image);
    if nodes.len > 1 { nodes.pop_n(1); }
    nodes
}

fn main() {
    var source: Node[>..] = [Node {}, Node {}];
    source[0].next .= source[1];
    let nodes: Node[] = shorten(source.to_bytes());
    print(nodes.len);
}
