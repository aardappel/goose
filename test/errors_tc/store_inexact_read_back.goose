// error: storing a reference rooted at x, which does not outlive the destination: it is reached through a reference that may point into n
// A reference read back out of h, a copy of an element of hs, may point
// into n or m (§9.5), so the field it rebinds may be n's, which x does not
// outlive (§9.2).
struct Node { p: i64? }
struct Holder { node: Node& }

fn main() {
    var n = Node { p: null };
    {
        var m = Node { p: null };
        var x: i64 = 5;
        var hs = [Holder { node: n }, Holder { node: m }];
        var h = hs[0];
        let r: Node& = h.node;
        r.p .= x;
    }
    if n.p { print(n.p); }
}
