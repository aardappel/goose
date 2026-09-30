// A holder copied out of the storage a parameter's class stands for holds
// views that storage holds, and so does a merge of such copies, a literal
// holding one and a call's result that is one, while a view read out of a
// copy the activation keeps is one of them: a shrink in the callee judges
// them by what the callee stored there, and leaves the rest to each call,
// which judges it by its record of the argument's storage (§5.1). Here none
// of them views the array shrunk.
struct H { s: const u8[:] }
struct Q { h: H, n: i64 }
struct N { v: i64, next: N&? }
enum E { Txt { s: const u8[:] }, Nothing { k: i64 } }
var TABLE: H[>..] = [];
var GOUT: u8[>..] = [];

// Merges of copies.
fn merged(out: u8[>..]&, hs: H[:], c: bool) {
    let h = if c { hs[0] } else { hs[1] };
    out.clear();
    out.append(h.s);
}
fn matched(out: u8[>..]&, hs: H[:], k: i64) {
    let h = match k { 0 => hs[0], _ => hs[1] };
    out.clear();
    out.append(h.s);
}
// Static text beside the copy, and copies out of two parameters' storage.
fn merged_text(out: u8[>..]&, hs: H[:], c: bool) {
    let h = if c { hs[0] } else { H { s: "lit" } };
    out.clear();
    out.append(h.s);
}
fn merged_two(out: u8[>..]&, a: H[:], b: H[:], c: bool) {
    let h = if c { a[0] } else { b[0] };
    out.clear();
    out.append(h.s);
}
// Literals holding copies.
fn in_literal(out: u8[>..]&, hs: H[:]) {
    let q = Q { h: hs[0], n: 1 };
    out.clear();
    out.append(q.h.s);
}
fn in_array(out: u8[>..]&, hs: H[:]) {
    let two = [hs[1], hs[0]];
    out.clear();
    out.append(two[0].s);
}
fn in_merged_literal(out: u8[>..]&, hs: H[:], c: bool) {
    let q = if c { Q { h: hs[0], n: 1 } } else { Q { h: hs[1], n: 2 } };
    out.clear();
    out.append(q.h.s);
}
// A popped element, and the holder a reference parameter names.
fn popped(out: u8[>..]&, hs: H[>..]&) {
    let q = Q { h: hs.pop(), n: 3 };
    out.clear();
    out.append(q.h.s);
}
fn loaded(out: u8[>..]&, r: H&, c: bool) {
    let h = if c { r } else { H { s: "lit" } };
    out.clear();
    out.append(h.s);
}
// Copies a call returns.
fn first(hs: H[:]) -> H { return hs[0]; }
fn wrapped(hs: H[:], c: bool) -> Q {
    if c { return Q { h: hs[1], n: 2 }; }
    return Q { h: first(hs), n: 3 };
}
fn from_call(out: u8[>..]&, hs: H[:]) {
    let h = first(hs);
    out.clear();
    out.append(h.s);
}
fn from_wrapped(out: u8[>..]&, hs: H[:], c: bool) {
    let q = wrapped(hs, c);
    out.clear();
    out.append(q.h.s);
}
// from_call's pair passes through mid.
fn mid(out: u8[>..]&, hs: H[:]) {
    from_call(out, hs);
    print(out);
}
// Views read out of copies before the shrink.
fn view_of_copy(out: u8[>..]&, hs: H[:]) {
    let h = hs[1];
    let s = h.s;
    out.clear();
    out.append(s);
}
fn view_of_reassigned(out: u8[>..]&, hs: H[:]) {
    var h = H { s: "" };
    h = hs[0];
    let s = h.s;
    out.clear();
    out.append(s);
}
fn view_of_literal(out: u8[>..]&, hs: H[:]) {
    let q = Q { h: hs[1], n: 1 };
    let s = q.h.s;
    out.clear();
    out.append(s);
}
// By-value binders, which nothing but their binding stores into.
fn view_of_binder(out: u8[>..]&, hs: H[:]) {
    for h in hs {
        for i in 0..1 {
            let s = h.s;
            out.clear();
            out.append(s);
            print(out);
        }
    }
}
fn view_of_payload(out: u8[>..]&, es: E[:]) {
    match es[0] {
        Txt t => {
            let s = t.s;
            out.clear();
            out.append(s);
        },
        Nothing n => print(n.k),
    }
}
// The node the caller linked lies outside the array shrunk.
fn follow(ns: N[>..]&) {
    let n = ns[1];
    let nx = n.next;
    ns.clear();
    if nx { print(nx.v); }
}
// A copy made on each iteration, read before the shrink.
fn view_each(out: u8[>..]&, hs: H[:]) {
    for i in 0..2 {
        let h = hs[i];
        let s = h.s;
        out.clear();
        out.append(s);
        print(out);
    }
}
// Through a nested function, a function value and a recursion.
fn nested_fn(out: u8[>..]&, hs: H[:], c: bool) {
    fn g() {
        let q = Q { h: if c { hs[0] } else { hs[1] }, n: 4 };
        out.clear();
        out.append(q.h.s);
    }
    g();
}
fn run<F>() { F(); }
fn in_block(out: u8[>..]&, hs: H[:]) {
    run() {
        let h = first(hs);
        let s = h.s;
        out.clear();
        out.append(s);
    };
}
recursive fn walk(out: u8[>..]&, hs: H[:], i: i64) {
    if i < hs.len {
        let h = if i == 0 { hs[i] } else { hs[0] };
        out.clear();
        out.append(h.s);
        walk(out, hs, i + 1);
        print(h.s);
    }
}

fn main() {
    var buf: u8[>..] = [];
    buf.append("abc");
    var hs: H[>..] = [];
    hs.push(H { s: "h0" });
    hs.push(H { s: "h1" });
    var other: u8[>..] = [];
    other.append("oo");
    var gs: H[>..] = [];
    gs.push(H { s: other[..] });
    gs.push(H { s: "g1" });
    var linked = N { v: 5, next: null };
    var nodes: N[>..] = [];
    nodes.push(N { v: 1, next: null });
    nodes.push(N { v: 2, next: null });
    nodes[1].next .= &linked;

    merged(buf, hs[..], true);
    print(buf);
    merged(buf, hs[..], false);
    print(buf);

    matched(buf, hs[..], 1);
    print(buf);

    merged_text(buf, hs[..], false);
    print(buf);

    merged_two(buf, hs[..], gs[..], false);
    print(buf);

    in_literal(buf, hs[..]);
    print(buf);

    in_array(buf, hs[..]);
    print(buf);

    in_merged_literal(buf, gs[..], true);
    print(buf);

    var more: H[>..] = [];
    more.push(H { s: "m0" });
    more.push(H { s: other[..] });
    popped(buf, more);
    print(buf, " ", more.len);

    var one = H { s: "r" };
    loaded(buf, one, true);
    print(buf);

    from_call(buf, hs[..]);
    print(buf);

    from_wrapped(buf, hs[..], true);
    print(buf);
    from_wrapped(buf, gs[..], false);
    print(buf);

    mid(buf, hs[..]);

    view_of_copy(buf, hs[..]);
    print(buf);

    view_of_reassigned(buf, gs[..]);
    print(buf);

    view_of_literal(buf, hs[..]);
    print(buf);

    view_of_binder(buf, gs[..]);

    var es: E[>..] = [];
    es.push(E.Txt { s: other[..] });
    view_of_payload(buf, es[..]);
    print(buf);

    follow(nodes);

    view_each(buf, gs[..]);

    nested_fn(buf, hs[..], false);
    print(buf);

    in_block(buf, gs[..]);
    print(buf);

    walk(buf, hs[..], 0);
    print(buf);

    TABLE.push(H { s: "t" });
    GOUT.append("g");
    from_call(GOUT, TABLE[..]);
    print(GOUT);
}
