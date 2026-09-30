// error: cannot clear a while v is still used: the slices it views may point into it
// v's elements view ss's elements, which view a (§5.1).
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(a[..]);
    var sss: ((u8[:])[:])[>..] = [];
    sss.push(ss[..]);
    let v = sss[..];
    a.clear();
    a.append("ZZZZZZZZ");
    print(v[0][0]);
}
