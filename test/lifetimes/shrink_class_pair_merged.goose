// A callee that shrinks through one parameter while it still uses a view
// passed as another is judged at each call by what the call passes (§5.2,
// §9.2): a merged reference beside a view of one of its arms is accepted where
// the callee is done with the view before the shrink, or where the view is of
// an array declared further in, which the merge cannot point at.
struct Rec { name: u8[], items: (const u8[:])[>..<] }

fn peek(p: (const u8[:])[:], r: Rec&) {
    print(p[0].len);
    r = Rec { name: "ABCDEFGHIJKLMNOPQRSTUVWXYZ", items: [] };
    print(r.items.len);
}

fn peek_first(p: i64[:], gs: i64[>..<]&) {
    print(p[0]);
    gs.clear();
    gs.push(5);
    print(gs);
}

fn peek_after(p: i64[:], gs: i64[>..<]&) {
    gs.clear();
    gs.push(6);
    print(p[0]);
}

// Passed on: the callers' arguments decide.
fn pass(q: i64[:], hs: i64[>..<]&) { peek_after(q, hs); }

fn main() {
    var a = Rec { name: "a", items: [] };
    a.items.push("ok");
    var b = Rec { name: "b", items: [] };
    let c = a.items.len > 0;
    let g: Rec& = if c { a } else { b };
    peek(a.items[..], g);
    print(a.items.len);

    var xs: i64[>..<] = [77];
    var ys: i64[>..<] = [];
    let h: i64[>..<]& = if xs.len > 0 { xs } else { ys };
    peek_first(xs[..], h);
    {
        var zs: i64[>..<] = [88, 89];
        peek_after(zs[..], h);
        pass(zs[1..], h);
    }
    print(xs);
}
