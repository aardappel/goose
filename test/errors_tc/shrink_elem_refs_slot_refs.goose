// error: cannot clear a while v is still used: what it views may hold a reference or slice into it
// v's elements refer to s, which views a (§5.1).
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var s = a[..];
    var rs: (u8[:]&)[>..] = [];
    rs.push(&s);
    let v = rs[..];
    a.clear();
    a.append("ZZZZZZZZ");
    print(v[0]);
}
