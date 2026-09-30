// error: cannot clear a while v is still used: the slices it views may point into it
// The block shrinks main's a and then reads main's v, whose elements view
// a (§5.1).
fn apply<F>() { F(); }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var ss: (u8[:])[>..] = [];
    ss.push(a[..]);
    let v = ss[..];
    apply() {
        a.clear();
        a.append("ZZZZZZZZ");
        print(v[0]);
    };
}
