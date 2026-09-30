// error: cannot clear buf while keep is still used: a reference into it was stored there
// hs views a1's elements or a2's: what the for binder copies out of it
// holds what either's do, and a1's element views buf (§5.1).
struct H { s: const u8[:] }
fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var a1: H[>..] = [];
    a1.push(H { s: buf[..] });
    var a2: H[>..] = [];
    a2.push(H { s: "x" });
    let c = buf.len > 5;
    let hs = if c { a2[..] } else { a1[..] };
    var keep = H { s: "" };
    for h in hs { keep = h; }
    buf.clear();
    buf.append("ZZZZZZZZ");
    print(keep.s);
}
