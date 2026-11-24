// error: copying a value of type Node[..2], which contains self-relative references
struct Node { next: Node&<u32>? }

fn main() {
    var rows: Node[..2][>..] = [];
    let image: u8[1] = [0];
    rows.resize(2, from_bytes<Node[]>(image));
}
