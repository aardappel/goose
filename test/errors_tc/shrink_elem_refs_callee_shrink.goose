// error: cannot call clr, which shrinks a while v is still used: the slices it views may point into it
// clr's shrink of a is the call's, and v's elements view a (§5.1).
fn clr(a: u8[>..]&) {
    a.clear();
    a.append("ZZZZZZZZ");
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(a[..]);
    let v = ss[..];
    clr(a);
    print(v[0]);
}
