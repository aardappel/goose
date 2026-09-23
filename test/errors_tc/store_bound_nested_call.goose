// error: call g stores a reference rooted at x into what its parameter h leads to, which may be n, which x does not outlive
// g passes its own h on, so what f stores into the storage behind it is g's
// to report to its callers, which check it against theirs (§9.2).
struct Node { p: i64? }
struct Holder { node: Node& }

fn f(h: Holder&, x: i64&) { h.node.p .= x; }
fn g(h: Holder&, x: i64&) { f(h, x); }

fn main() {
    var n = Node { p: null };
    {
        var x: i64 = 5;
        var h = Holder { node: n };
        g(h, x);
    }
    if n.p { print(n.p); }
}
