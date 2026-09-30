// error: cannot call f: it shrinks a while v is still used, and v may refer into a
// bs's elements view the array b names, which f tells apart from a's only
// as its callers do: this call may pass a for both (§5.1).
struct B { s: u8[:] }
fn f(a: u8[>..]&, b: u8[>..]&) {
    var bs: B[>..] = [];
    bs.push(B { s: b[..] });
    let v = bs[..];
    a.clear();
    a.append("ZZZZZZZZ");
    print(v[0].s);
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var c: u8[>..] = [];
    c.append("def");
    f(a, if a.len > 1 { a } else { c });
}
