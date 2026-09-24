// error: storing a reference into doc, which holds a grow-shrink array
// A back edge's holder result holds what the return checked before it
// holds (§7.8): a holder read out of `doc`, whose grow-shrink bytes it may
// view as far as the types tell, so no store may keep it (§5.2).
struct Fragment { text: const u8[:] }
struct Doc { frag: Fragment, bytes: u8[>..<] }
var doc = Doc { frag: Fragment { text: "doc" }, bytes: [] };
var kept: Fragment[>..] = [];

recursive fn f(k: i64) -> Fragment {
    if k == 0 { return doc.frag; }
    let child = f(k - 1);
    kept.push(child);
    return child;
}

fn main() {
    print(f(2).text);
    print(kept.len);
}
