// error: cannot construct Node[>..<] from Node[]: a shrink could remove an element
struct Node { next: Node&<u32>? }

fn decode(image: u8[:]) -> Node[] {
    let nodes = from_bytes<Node[]>(image);
    nodes
}

fn maybe_decode(image: u8[:], load: bool) -> bool, Node[] {
    return load, if load { decode(image) } else { [] };
}

fn forward(image: u8[:]) -> bool, Node[>..<] { return maybe_decode(image, true); }

fn main() {
    let image: u8[1] = [0];
    let load, nodes = forward(image);
    print(load, nodes.len);
}
