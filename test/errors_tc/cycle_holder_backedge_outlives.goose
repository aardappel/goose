// error: storing a reference rooted at s, which does not outlive the destination
// A holder result is given to back edges by the roots of what it holds
// (§7.8): the first return holds a view of the parameter, so the back edge's
// result holds a view of what it passes there, the caller's `word`, which a
// global may not keep.
struct Fragment { text: const u8[:] }
var kept: Fragment[>..] = [];

recursive fn first(s: const u8[:], n: i64) -> Fragment {
    if n == 0 { return Fragment { text: s[..1] }; }
    let child = first(s[1..], n - 1);
    kept.push(child);
    return child;
}

fn main() {
    var word: u8[>..] = "goose";
    print(first(word, 2).text);
}
