// error: cannot clear a while v is still used: the slices it views may point into it
// v views ss's elements, slices the push made views of a (§5.1).
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(a[..]);
    let v = ss[..];
    a.clear();
    a.append("ZZZZZZZZ");
    print(v[0]);
}
