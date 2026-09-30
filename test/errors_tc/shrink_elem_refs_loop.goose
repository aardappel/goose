// error: cannot clear a while v is still used: the slices it views may point into it
// The first iteration stores a view of a into ss through v, and the last
// reads it after the second has cleared a (§5.1).
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var x: u8[>..] = [];
    x.append("xyz");
    var ss: (u8[:])[>..] = [];
    ss.push(x[..]);
    let v = ss[..];
    for i in 3 {
        if i == 2 { print(v[0]); }
        a.clear();
        a.append(if i == 0 { "abc" } else { "ZZZZZZZZ" });
        if i == 0 { v[0] = a[..]; }
    }
}
