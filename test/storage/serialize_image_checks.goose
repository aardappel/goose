// from_bytes verifies an untrusted image before it becomes a value (§12):
// non-canonical varints, bools and length prefixes are rejected while their
// canonical forms load, and a reference offset as large as INT64_MAX is
// rejected without overflowing.
struct V { x: varint }
struct B { flags: bool[2] }
struct Node {
    tag: u8,
    next: Node&<u64>?
}

fn serialize_canonical() {
    let badvar: u8[3] = [2, 128, 0];
    let vs, vok = from_bytes<V[>..]>(badvar);
    assert(!vok && vs.len == 0);
    let badbool: u8[2] = [1, 2];
    let bs, bok = from_bytes<bool[>..]>(badbool);
    assert(!bok && bs.len == 0);
    let nested: u8[3] = [2, 0, 255];
    let ns, nok = from_bytes<B[>..]>(nested);
    assert(!nok && ns.len == 0);
    let badprefix: u8[2] = [128, 0];
    let ps, pok = from_bytes<u8[>..]>(badprefix);
    assert(!pok && ps.len == 0);
    let values: V[>..] = [V { 0 }, V { -1 }, V { 64 }, V { -9223372036854775808 }];
    let image = to_bytes(values);
    let copy, ok = from_bytes<V[>..]>(image);
    assert(ok && values == copy);
    let goodbool: u8[3] = [2, 0, 1];
    let good, goodok = from_bytes<bool[>..]>(goodbool);
    assert(goodok && !good[0] && good[1]);
    print("canonical images verified");
}

fn serialize_offset_overflow() {
    // The reference field begins at byte 1 and holds INT64_MAX.
    // Reject the image without overflowing when locating its target.
    let image: u8[10] = [9, 0, 255, 255, 255, 255, 255, 255, 255, 127];
    let nodes,
    ok = from_bytes<Node[>..]>(image);
    assert(!ok && nodes.len == 0);
    print(ok, " ", nodes.len);
}

fn main() {
    serialize_canonical();
    serialize_offset_overflow();
}
