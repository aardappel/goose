// References to nested resizable values are rejected: a resizable's header
// lives with its owning variable (C.2).
struct Tailed { count: int, items: int[>..] }
fn main() {
    var t = Tailed { count: 0, items: [1] };
    let r: int[>..]& = &t.items;
    r.push(2);
    print(t.items.len);
}
