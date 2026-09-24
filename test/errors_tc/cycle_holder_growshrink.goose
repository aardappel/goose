// error: this return may point into a grow-shrink array, but the recursive cycle already used a result that does not
// A back edge is given what the returns checked before it hold (§7.8): here
// a string literal, so its result may be kept. The return checked after it
// is a holder read out of `doc`, whose grow-shrink bytes it may view as far
// as the types tell, which no store may keep (§5.2), so it would take back
// what the back edge was given.
struct Fragment { text: const u8[:] }
struct Doc { frag: Fragment, bytes: u8[>..<] }
var doc = Doc { frag: Fragment { text: "doc" }, bytes: [] };
var kept: Fragment[>..] = [];

recursive fn f(k: i64) -> Fragment {
    if k == 0 { return Fragment { text: "static" }; }
    let child = f(k - 1);
    kept.push(child);
    return doc.frag;
}

fn main() {
    print(f(2).text);
    print(kept.len);
}
