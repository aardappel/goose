// error: retained storage may have a self-relative link to a removed element
struct Node { next: Node&<u32>? }

fn main() {
    var source: Node[>..] = [Node {}, Node {}];
    source[0].next .= source[1];
    var nodes = from_bytes<Node[>..]>(source.to_bytes());
    nodes.pop_n(1);
    print(nodes.len);
}
