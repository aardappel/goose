// error: its links, read out of this copy, would be measured from the copy
struct Node { next: Node&<u32>? }

fn same(node: Node&) -> bool {
    node == ({ print(1); node })
}
fn main() {
    let node = Node { next: null };
    print(same(node));
}
