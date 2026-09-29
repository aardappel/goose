// error: storing a slice rooted at b into s through a reference: s is bound to one rooted at a, at a different scope depth
// r names s itself (§3.8, §4.1), so storing b's view through it assigns s,
// whose binding is a view of the inner block's a: a slice variable is bound
// only to roots at one depth (§9.2), however it is assigned. Accepted, the
// store would leave s viewing b's elements after b.clear() (§5.1).
fn main() {
    var b: u8[>..] = [];
    b.append("xyz");
    {
        var a: u8[>..] = [];
        a.append("ab");
        var s = a[..];
        let r: u8[:]& = s;
        r = b[..];
        b.clear();
        b.append("QQQ");
        print(s);
    }
}
