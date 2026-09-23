// error: storing a reference rooted at x, which does not outlive the destination: it is reached through a reference that may point into a1
// r's root is a merge of a1's and a2's, which only bounds where it points:
// the field it rebinds may be a1's, which x does not outlive (§9.2).
struct Node { p: i64? }

fn main() {
    var a1 = Node { p: null };
    var c = true;
    {
        var a2 = Node { p: null };
        let r: Node& = if c { a1 } else { a2 };
        var x: i64 = 5;
        r.p .= x;
    }
    if a1.p { print(a1.p); }
}
