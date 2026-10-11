// error: cannot call reset, which shrinks b while v is still used: it may hold a reference or slice into it (§5.1)
// Assigning the value whole through a reference shrinks the grow-only array
// it holds, which each call judges against the caller's views (§4.4, §5.1).
struct Box { tag: i64, items: u8[>..] }
fn fresh() -> Box { return Box { tag: 2, items: [] }; }
fn reset(r: Box&) { r = fresh(); }
fn main() {
    var b = Box { tag: 1, items: [] };
    b.items.append("abcd");
    let v = b.items[1..];
    reset(b);
    print(v);
}
