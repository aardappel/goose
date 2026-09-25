// Root sets (§9.2): a value that may point at several places points at each
// of them and at nothing else, so a shrink of an array it cannot be in
// passes where a single root bounding a scope would have stopped it.
struct H { s: const u8[:] }

fn main() {
    var a: u8[>..] = "aaa";
    var b: u8[>..] = "bbbb";
    var c: u8[>..] = "ccccc";
    let k = a.len < b.len;
    // An if's value may be a view of a or of b: both are its roots, and the
    // holder it is stored into holds both.
    let v: const u8[:] = if k { a[..] } else { b[..] };
    var h = H { s: v };
    c.pop();
    print(v, " ", h.s, " ", c);
    // A slice read out of a container may point into any array in scope that
    // can hold its elements when it is read: a, b or c, not one declared
    // later.
    var hs: H[>..] = [];
    hs.push(H { s: b[..] });
    let r = hs[0].s;
    var d: u8[>..] = "dd";
    d.pop();
    print(r, " ", d);
}
