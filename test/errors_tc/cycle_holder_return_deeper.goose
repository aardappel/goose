// error: this return's reference is rooted at mine, deeper than the result the recursive cycle's back edges were given
// A holder result's returns are checked against what back edges were given
// (§7.8): the first return holds a string literal, so the back edge's
// result may be kept in a global; the second holds a view of `mine`, which
// dies with `walk`, after that result was kept.
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
