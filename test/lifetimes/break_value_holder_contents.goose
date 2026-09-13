// A holder read out of a container and passed out of a block or loop by a
// valued break still has that container's contents, so a buffer the container
// never referred into can be cleared while the holder is live (§5.1).
struct ByteRef { r: u8& }
fn main() {
    var buf: u8[>..] = [];
    var other: u8[>..] = [];
    buf.push(1);
    other.push(2);
    var refs: ByteRef[>..] = [];
    refs.push(ByteRef { r: other[0] });
    let a = block { break refs[0]; };
    let b = loop { break refs[0]; };
    var kept: ByteRef[>..] = [];
    kept.push(block { break refs[0]; });
    buf.clear();
    print(a.r, " ", b.r, " ", kept[0].r);
}
