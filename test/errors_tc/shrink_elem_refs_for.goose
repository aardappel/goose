// error: cannot clear a while v is still used: the slices it views may point into it
// The loop's next iteration reads v's next element, a view of a (§5.1).
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(a[..]);
    ss.push(a[..]);
    let v = ss[..];
    for i in v.len {
        print(v[i]);
        a.clear();
        a.append("ZZZZZZZZ");
    }
}
