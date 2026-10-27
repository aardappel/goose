// error: storing a reference rooted at x, which does not outlive the destination
// g passes its own h on to f. h names main's local holder, whose node is n,
// and that record is kept through both calls (§9.5), so f's rebind of the
// node's field is checked against n, which x does not outlive (§9.2).
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
