// error: storing a reference that may point into doc, which holds a grow-shrink array
// A cycle's result has every root its returns give (§7.8): here a string
// literal, and a holder read out of `doc`, whose grow-shrink bytes it may
// view as far as the types tell, which no store may keep (§5.2).
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
