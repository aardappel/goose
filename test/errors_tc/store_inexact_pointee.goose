// error: storing a reference rooted at x, which does not outlive the destination: it is reached through a reference that may point into a1
// Assigning through r writes whichever of a1 and a2 it names, and the
// literal holds a reference to x, which a1 would outlive (§9.2).
struct Node { p: i64? }

fn main() {
    var a1 = Node { p: null };
    var c = true;
    {
        var a2 = Node { p: null };
        let r: Node& = if c { a1 } else { a2 };
        var x: i64 = 5;
        r = Node { p: x };
    }
    if a1.p { print(a1.p); }
}
