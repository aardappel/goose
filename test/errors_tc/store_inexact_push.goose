// error: storing a reference rooted at x, which does not outlive the destination: it is reached through a reference that may point into b1
// The array pushed into through r may be b1's, whose elements would then
// outlive x (§9.2).
struct Bag { n: i64, refs: i64&[>..] }

fn main() {
    var b1 = Bag { n: 0, refs: [] };
    var c = true;
    {
        var b2 = Bag { n: 0, refs: [] };
        let r: Bag& = if c { b1 } else { b2 };
        var x: i64 = 5;
        r.refs.push(x);
    }
    for p in b1.refs { print(p); }
}
