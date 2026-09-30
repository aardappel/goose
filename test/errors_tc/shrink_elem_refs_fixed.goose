// error: cannot clear a while v is still used: the slices it views may point into it
// ss's literal holds a view of a, which v's elements are (§5.1).
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (u8[:])[1] = [a[..]];
    let v = ss[..];
    a.clear();
    a.append("ZZZZZZZZ");
    print(v[0]);
}
