// error: call f stores a reference rooted at x through its parameter r, whose argument may point into a1, which x does not outlive
// f's body sees one array behind r, as deep as a2; the argument may be a1,
// which the call checks the stored reference against (§9.2).
struct Node { p: i64? }

fn f(r: Node&, x: i64&) { r.p .= x; }

fn main() {
    var a1 = Node { p: null };
    var c = true;
    {
        var a2 = Node { p: null };
        let r: Node& = if c { a1 } else { a2 };
        var x: i64 = 5;
        f(r, x);
    }
    if a1.p { print(a1.p); }
}
