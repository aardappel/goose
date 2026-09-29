// error: cannot clear a while v is still used: a reference to a slice that may point into it was stored there
// The binder is a copy of e's payload, whose field refers to s, which views
// a (§5.1).
struct R { r: u8[:]& }
enum E { V { x: R }, N }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var s = a[..];
    var e: E = E.V { x: R { r: s } };
    match e {
        V v => {
            a.clear();
            a.append("ZZZZZZZZ");
            print(v.x.r);
        },
        N => print("n")
    }
}
