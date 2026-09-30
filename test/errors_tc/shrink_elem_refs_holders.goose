// error: cannot clear a while v is still used: what it views may hold a reference or slice into it
// v views bs's elements, whose field the push made a view of a (§5.1).
struct B { s: u8[:] }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var bs: B[>..] = [];
    bs.push(B { s: a[..] });
    let v = bs[..];
    a.clear();
    a.append("ZZZZZZZZ");
    print(v[0].s);
}
