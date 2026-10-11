struct Node {
    tag: u8,
    next: Node&<u64>?
}

fn main() {
    // The reference field begins at byte 1 and holds INT64_MAX.
    // Reject the image without overflowing when locating its target.
    let image: u8[10] = [9, 0, 255, 255, 255, 255, 255, 255, 255, 127];
    let nodes,
    ok = from_bytes<Node[>..]>(image);
    assert(!ok && nodes.len == 0);
    print(ok, " ", nodes.len);
}
