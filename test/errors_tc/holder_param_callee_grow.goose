// error: cannot call mk, which grows ws: the element pushed into ws is still under construction
// mk's result is built in place at ws's top, and g, which it calls halfway
// through, pushes into the array its by-value holder parameter's reference
// points at: for mk's caller, a growth of what mk's holder points into, and
// for main's, a growth of ws, not of the holder (§1.3(4)). The pushed string
// would land inside the element, and reading ws back crashes.
struct H { r: u8[][>..]& }
fn id(h: H) -> H { h }
fn g(v: H) -> u8 { let r .= id(v).r; r.push("zzzzzzzz"); 66 }
fn mk(v: H) -> u8[] { [65, g(v), 67] }
fn main() {
    var ws: u8[][>..] = [];
    ws.push("first");
    ws.push(mk(H { r: ws }));
    for w in ws { print(w); }
}
