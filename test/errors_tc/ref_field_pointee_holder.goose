// error: storing a reference rooted at x, which does not outlive the destination
// Writing a value through a reference read out of a field stores what it
// holds where that reference points: into a, which x does not outlive (§9.2).
struct Node { p: i64? }
struct R { n: Node& }

fn main() {
    var a = Node { p: null };
    {
        let h = R { n: a };
        var x: i64 = 5;
        h.n = Node { p: x };
    }
    if a.p { print(a.p); }
}
