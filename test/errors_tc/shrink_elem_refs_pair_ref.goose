// error: cannot call f: it shrinks a while r is still used, and r may refer into a
// r refers to h, whose field views ss's elements, which view the array b
// names: f tells it apart from a's only as its callers do, and this call
// may pass a for both (§5.1).
struct R2 { v: (u8[:])[:] }
fn f(a: u8[>..]&, b: u8[>..]&) {
    var ss: (u8[:])[>..] = [];
    ss.push(b[..]);
    var h = R2 { v: ss[..] };
    let r = &h;
    a.clear();
    a.append("ZZZZZZZZ");
    print(r.v[0]);
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var c: u8[>..] = [];
    c.append("def");
    f(a, if a.len > 1 { a } else { c });
}
