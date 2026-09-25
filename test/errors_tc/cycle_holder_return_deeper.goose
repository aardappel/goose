// error: storing a reference rooted at mine, which does not outlive the destination
// A holder result's roots are those of every return (§7.8): the first holds
// a string literal, the second a view of `mine`, which dies with `walk`, so
// the result may not be kept in a global.
struct Fragment { text: const u8[:] }
var kept: Fragment[>..] = [];

fn walk(n: i64) {
    var mine: u8[>..] = "mine";
    recursive fn f(k: i64) -> Fragment {
        if k == 0 { return Fragment { text: "static" }; }
        let child = f(k - 1);
        kept.push(child);
        return Fragment { text: mine[..] };
    }
    print(f(n).text);
}

fn main() {
    walk(2);
    print(kept[0].text);
}
