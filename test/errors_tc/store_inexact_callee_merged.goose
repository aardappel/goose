// error: storing a reference rooted at x, which does not outlive the destination: it is reached through a reference that may point into a
// In f, r may name a's or b's storage, and the caller's a1, behind a, dies
// after x does (§9.2).
struct Node { p: i64? }

fn f(a: Node&, b: Node&, x: i64&, c: bool) {
    let r: Node& = if c { a } else { b };
    r.p .= x;
}

fn main() {
    var a1 = Node { p: null };
    {
        var a2 = Node { p: null };
        var x: i64 = 5;
        f(a1, a2, x, true);
    }
    if a1.p { print(a1.p); }
}
