// A valued break, out of a block or a loop, hands on what it breaks with as a
// block's tail would: a slice keeps the root of the array it views.
fn block_view(a: i64[:]) -> i64[:] { block { break a; } }
fn loop_view(a: i64[:]) -> i64[:] { loop { break a; } }
fn choose(a: i64[:], first: bool) -> i64[:] {
    block { if first { break a; } a }
}
fn provenance() {
    var a: i64[2] = [42, 43];
    let x = block_view(a[..]);
    let y = loop_view(a[..]);
    x[0] = 44;
    print(y[0]);
    print(choose(a[..], false)[1]);
    let n = block { if a[0] == 44 { break 7; } 9 };
    print(n);
}

// A holder read out of a container and passed out of a block or loop by a
// valued break still has that container's contents, so a buffer the container
// never referred into can be cleared while the holder is live (§5.1).
struct ByteRef { r: u8& }
fn holder_contents() {
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

fn main() {
    provenance();
    holder_contents();
}
