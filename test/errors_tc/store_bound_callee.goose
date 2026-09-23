// error: call f stores a reference rooted at x into what its parameter h leads to, which may be n, which x does not outlive
// f rebinds a field of the node h's holder points at, storage of the
// caller's that f cannot see, only bound by h; here it is n, which x does
// not outlive (§9.2, §9.5).
struct Node { p: i64? }
struct Holder { node: Node& }

fn f(h: Holder&, x: i64&) { h.node.p .= x; }

fn main() {
    var n = Node { p: null };
    {
        var x: i64 = 5;
        var h = Holder { node: n };
        f(h, x);
    }
    if n.p { print(n.p); }
}
