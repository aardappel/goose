// error: its links, read out of this copy, would be measured from the copy
struct Node { next: Node&<u32>? }

fn differs(node: Node&, skip: bool) -> bool {
    (if skip { return false; } else { node }) != node
}
fn main() {
    let node = Node { next: null };
    print(differs(node, false));
}
