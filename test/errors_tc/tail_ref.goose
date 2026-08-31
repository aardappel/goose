// References to nested resizable values are rejected: a resizable's header
// lives with its owning variable (C.2).
struct Tailed { count: i64, items: i64[>..] }
fn main() {
    var t = Tailed { count: 0, items: [1] };
    let r: i64[>..]& = &t.items;
    r.push(2);
    print(t.items.len);
}
