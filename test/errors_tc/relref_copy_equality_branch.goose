// error: its links, read out of this copy, would be measured from the copy
struct Node { next: Node&<u32>? }

fn differs(a: Node&, b: Node&, pick: bool) -> bool {
    a != (if pick { a } else { b })
}
fn main() {
    let n = Node { next: null };
    print(differs(n, n, true));
}
