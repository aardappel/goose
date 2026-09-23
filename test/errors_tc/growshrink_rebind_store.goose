// error: storing a reference that may point into a, which holds a grow-shrink array
// A rebind to another root at the same depth leaves a reference variable
// rooted at the new one (§9.2), but it may still hold its first binding, an
// element of the grow-shrink `a`, which is never stored (§5.2).
struct K { r: u8& }

fn main() {
    var a: u8[>..<] = "xyz";
    var b: u8[3] = [97, 98, 99];
    var hs: K[>..] = [];
    var r .= a[0];
    if hs.len == 1 { r .= b[0]; }
    hs.push(K { r: r });
    a.clear();
    a.append("pqr");
    print(hs[0].r);
}
