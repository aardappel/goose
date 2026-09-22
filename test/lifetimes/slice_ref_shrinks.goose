// A reference to a slice variable, bound by reference or with an explicit
// `&`, a parameter or a local, keeps the slice live only as long as it is
// used: an array its slice views shrinks after the reference's last use
// (§5.1, §5.2). A reference to a holder keeps what the holder's stores put
// there live, and nothing else.
struct Rec { name: u8[], items: (const u8[:])[>..<] }
struct H { s: i64[:] }

fn peek(p: (const u8[:])[:]&, r: Rec&) {
    print(p[0]);
    r = Rec { name: "replaced", items: [] };
    print(r.name, " ", r.items.len);
}

fn look(p: i64[:]&, gs: i64[>..<]&) {
    let v = p;
    print(v[0], " ", p.len);
    gs.clear();
    gs.push(5);
    print(gs[0]);
}

fn main() {
    // Through parameters, bound by reference and with an explicit `&`.
    var r = Rec { name: "kept", items: [] };
    r.items.push("ok");
    var s = r.items[..];
    peek(s, r);
    var gs: i64[>..<] = [42];
    var t = gs[..];
    look(t, gs);
    var u = gs[..];
    look(&u, gs);

    // Locals, a `for` through one, and a slice loaded out of one.
    var w = gs[..];
    let q: i64[:]& = w;
    print(q[0]);
    let e = &w;
    for x in e { print(x); }
    let loaded = e;
    print(loaded.len);
    gs.clear();
    print(gs.len);

    // Grow-only: a reference to a slice variable, to a slice field, and to
    // a holder of slices into another array.
    var xs: i64[>..] = [7, 8];
    var v = xs[..];
    let pv = &v;
    print(pv[1]);
    var h = H { s: xs[1..] };
    let ps: i64[:]& = h.s;
    print(ps[0]);
    xs.clear();
    print(xs.len);
    var ys: i64[>..] = [9];
    var views: (i64[:])[>..] = [];
    views.push(ys[..]);
    let all: (i64[:])[>..]& = views;
    xs.push(1);
    xs.clear();
    print(all[0][0], " ", xs.len);
}
