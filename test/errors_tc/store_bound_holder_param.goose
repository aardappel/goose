// error: call f stores a reference rooted at x into what its parameter h2 leads to, which may be n, which x does not outlive
// The node f rebinds a field of is two references away from its by-value
// holder h2, whose class bounds it only by the holder h, as deep as x; the
// call finds n behind h (§9.2, §9.5).
struct Node { p: i64? }
struct Holder { node: Node& }
struct Holder2 { h: Holder& }

fn f(h2: Holder2, x: i64&) { h2.h.node.p .= x; }

fn main() {
    var n = Node { p: null };
    {
        var x: i64 = 5;
        var h = Holder { node: n };
        let h2 = Holder2 { h: h };
        f(h2, x);
    }
    if n.p { print(n.p); }
}
