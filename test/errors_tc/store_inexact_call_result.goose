// error: storing a reference rooted at x, which does not outlive the destination: it is reached through a reference that may point into a1
// pick returns one of its arguments, so its result is only bounded by a2's
// root: the field rebound through it may be a1's, which x does not outlive.
struct Node { p: i64? }

fn pick(a: Node&, b: Node&, c: bool) -> Node& { if c { a } else { b } }

fn main() {
    var a1 = Node { p: null };
    var c = true;
    {
        var a2 = Node { p: null };
        var x: i64 = 5;
        let r: Node& = pick(a1, a2, c);
        r.p .= x;
    }
    if a1.p { print(a1.p); }
}
