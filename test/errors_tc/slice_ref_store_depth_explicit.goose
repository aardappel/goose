// error: storing a slice rooted at b into s through a reference: s is bound to one rooted at a, at a different scope depth
// Taken with an explicit `&`, r names s as well (§3.8): the store through it
// assigns s, whose binding is a view of the inner block's a (§9.2).
fn main() {
    var b: u8[>..] = [];
    b.append("xyz");
    {
        var a: u8[>..] = [];
        a.append("ab");
        var s = a[..];
        let r = &s;
        r = b[..];
        b.clear();
        b.append("QQQ");
        print(s);
    }
}
