// error: cannot call f: it shrinks a while v is still used, and v may refer into a
// v's elements view ss's, which view the array b names: f tells it apart
// from a's only as its callers do, and this call may pass a for both (§5.1).
fn f(a: u8[>..]&, b: u8[>..]&) {
    var ss: (u8[:])[>..] = [];
    ss.push(b[..]);
    var sss: ((u8[:])[:])[>..] = [];
    sss.push(ss[..]);
    let v = sss[..];
    a.clear();
    a.append("ZZZZZZZZ");
    print(v[0][0]);
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var c: u8[>..] = [];
    c.append("def");
    f(a, if a.len > 1 { a } else { c });
}
