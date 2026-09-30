// error: cannot clear a while v is still used: the slices it views may point into it
// v views ss's elements or tt's, and ss's view a (§5.1).
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(a[..]);
    var x: u8[>..] = [];
    x.append("x");
    var tt: (u8[:])[>..] = [];
    tt.push(x[..]);
    let v = if a.len > 1 { ss[..] } else { tt[..] };
    a.clear();
    a.append("ZZZZZZZZ");
    print(v[0]);
}
