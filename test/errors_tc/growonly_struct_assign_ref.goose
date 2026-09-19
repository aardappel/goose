// error: cannot assign a value holding a grow-only array through a reference
// Assigning the value whole shrinks the array it holds, which only the
// function owning a grow-only array may do (§5.1).
struct Box { tag: i64, items: u8[>..] }
fn fresh() -> Box { return Box { tag: 2, items: [] }; }
fn reset(r: Box&) { r = fresh(); }
fn main() {
    var b = Box { tag: 1, items: [] };
    b.items.append("abcd");
    reset(b);
    print(b.items);
}
