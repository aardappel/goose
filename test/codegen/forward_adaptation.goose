// A multi-value call forwarded by a return (§7.1), to the function's own
// caller or to a function further out (`return f() from g`, §7.9), whose
// results the checker fits to the returning function's result types one by
// one: integers to wider integers and to floats, f32 to f64, a variant to
// its ADT in either mode (a fixed-size, a variable-size and a resizable
// variant), an ADT to its other mode, a reference to its pointee or to an
// optional, a reference to an array to a slice, and an array or a slice to
// an array of another kind or length storage; in the result C returns and in
// out-parameters, from a function and from case functions dispatched on a
// tag. A variant that is not fixed-size builds its variable-mode ADT behind
// the tag, and a return taken in the forwarded call's argument after that
// tag is written delivers its own value. The functions in between are
// recursive, so -O2 inlines none of them into its target, and every
// long-distance return stays one.
struct P { a: i64, b: i64 }
enum E { A { n: i64 }, B { s: i64 } }
enum Msg { Ping, Text { body: u8[] } }
enum Value { V { items: i64[>..] }, W { n: i64 }, X { s: u8[] } }
enum Pick { First { k: i64 }, Second { k: i64 } }

var gp = P { 1, 2 };
var gx = 5;
var garr: u8[4] = [97, 98, 99, 100];
var gs: u8[4] = [104, 105, 106, 107];

fn halves() -> i32, i32 { return -5, -6; }
fn small() -> u8, i16 { return 200, -300; }
fn ints() -> i64, i32 { return 1 << 40, -7; }
fn flts() -> f32, f32 { return 1.5, 2.25; }
fn refs() -> P&, i64& { return gp, gx; }
fn view() -> u8[4]&, i64& { return garr, gx; }
fn variant() -> E.B, i64 { return E.B { s: 3 }, 4; }
fn late() -> i64, E.A { return 5, E.A { n: 6 }; }
fn fixedadt() -> E, i64 { return E.B { s: 7 }, 8; }
fn varadt() -> E.., i64 { return E.A { n: 9 }, 10; }
fn text(n: i64) -> Msg.Text, i64 { return Msg.Text { body: "hey" }, n; }
fn items(n: i64) -> Value.V, i64 { return Value.V { items: [1, 2, n] }, n; }
fn plain() -> Value.W, i64 { return Value.W { n: 12 }, 13; }
fn named() -> Value.X, i64 { return Value.X { s: "xs" }, 14; }
fn chars() -> i64, u8[] { return 1, "abc"; }
fn wide() -> u8[u16], i64 { return "hij", 2; }
fn grow() -> u8[>..], i64 { var s: u8[>..] = []; s.append("defg"); return s, 3; }
fn part() -> u8[:], i64 { return gs[1..3], 4; }
fn lim() -> u8[..], i64 { var l: u8[..] = [..6]; l.append("lm"); return l, 5; }
fn three() -> i64[3], i64 { return [7, 8, 9], 6; }
fn more() -> i64[>..], i64 { var s: i64[>..] = [4, 5]; return s, 7; }
fn caps() -> i64[3], u8[>..] { var s: u8[>..] = []; s.append("xyz"); return [1, 2, 3], s; }
fn pick(c: Pick.First) -> i32, i32 { return c.k as i32, -1; }
fn pick(c: Pick.Second) -> i32, i32 { return -2, c.k as i32; }

// Each `to_` function returns its target's results from below it; k picks
// the call.
recursive fn to_wide(k: i64, n: i64) {
    if n > 0 { to_wide(k, n - 1); }
    if k == 0 { return halves() from widened; }
    return small() from widened;
}
fn widened(k: i64) -> i64, i64 { to_wide(k, 1); return 0, 0; }
recursive fn to_float(k: i64, n: i64) {
    if n > 0 { to_float(k, n - 1); }
    if k == 0 { return ints() from floats; }
    return flts() from floats;
}
fn floats(k: i64) -> f64, f64 { to_float(k, 1); return 0, 0; }
recursive fn to_pointee(n: i64) { if n > 0 { to_pointee(n - 1); } return refs() from pointees; }
fn pointees() -> P, i64 { to_pointee(1); return P { 0, 0 }, 0; }
recursive fn to_view(n: i64) { if n > 0 { to_view(n - 1); } return view() from views; }
fn views() -> u8[:], i64? { to_view(1); return garr[0..0], null; }
recursive fn to_fixed(k: i64, n: i64) {
    if n > 0 { to_fixed(k, n - 1); }
    if k == 0 { return variant() from fixed; }
    return varadt() from fixed;
}
fn fixed(k: i64) -> E, i64 { to_fixed(k, 1); return E.A { n: 0 }, 0; }
recursive fn to_later(n: i64) { if n > 0 { to_later(n - 1); } return late() from later; }
fn later() -> i64, E { to_later(1); return 0, E.A { n: 0 }; }
recursive fn to_varmode(k: i64, n: i64) {
    if n > 0 { to_varmode(k, n - 1); }
    if k == 0 { return variant() from varmode; }
    return fixedadt() from varmode;
}
fn varmode(k: i64) -> E.., i64 { to_varmode(k, 1); return E.A { n: 0 }, 0; }
recursive fn to_texts(n: i64) { if n > 0 { to_texts(n - 1); } return text(11) from texts; }
fn texts() -> Msg.., i64 { to_texts(1); return Msg.Ping, 0; }
recursive fn to_values(k: i64, n: i64) {
    if n > 0 { to_values(k, n - 1); }
    if k == 0 { return items(3) from values; }
    if k == 1 { return plain() from values; }
    return named() from values;
}
fn values(k: i64) -> Value.., i64 { to_values(k, 1); return Value.W { n: 0 }, 0; }
recursive fn to_prefixed(n: i64) { if n > 0 { to_prefixed(n - 1); } return chars() from prefixed; }
fn prefixed() -> i64, u8[varint] { to_prefixed(1); return 0, ""; }
recursive fn to_flat(k: i64, n: i64) {
    if n > 0 { to_flat(k, n - 1); }
    if k == 0 { return wide() from flat; }
    if k == 1 { return grow() from flat; }
    if k == 2 { return part() from flat; }
    return lim() from flat;
}
fn flat(k: i64) -> u8[], i64 { to_flat(k, 1); return "", 0; }
recursive fn to_growing(k: i64, n: i64) {
    if n > 0 { to_growing(k, n - 1); }
    if k == 0 { return wide() from growing; }
    return part() from growing;
}
fn growing(k: i64) -> u8[>..], i64 { to_growing(k, 1); return [], 0; }
recursive fn to_shrinking(k: i64, n: i64) {
    if n > 0 { to_shrinking(k, n - 1); }
    if k == 0 { return three() from shrinking; }
    return more() from shrinking;
}
fn shrinking(k: i64) -> i64[>..<], i64 { to_shrinking(k, 1); return [], 0; }
recursive fn to_limited(n: i64) { if n > 0 { to_limited(n - 1); } return caps() from limited; }
fn limited() -> i64[..4], u8[..8] { to_limited(1); return [], ""; }
recursive fn to_picked(c: Pick, n: i64) {
    if n > 0 { to_picked(c, n - 1); }
    return pick(c) from picked;
}
fn picked(c: Pick) -> i64, f64 { to_picked(c, 1); return 0, 0; }

// Forwarded to the function's own caller.
fn own_texts() -> Msg.., i64 { return text(21); }
fn own_values() -> Value.., i64 { return items(4); }

// A return taken in the argument, after the tag is at the destination.
fn early(k: i64) -> Msg.., i64 {
    return text({ if k > 5 { return Msg.Text { body: "early" }, 1; } k });
}
fn early_items(k: i64) -> Value.., i64 {
    return items({ if k > 5 { return Value.V { items: [9, 9] }, 1; } k });
}
recursive fn to_far(k: i64, n: i64) {
    if n > 0 { to_far(k, n - 1); }
    return text({ if k > 5 { return Msg.Text { body: "far" }, 2 from far; } k }) from far;
}
fn far(k: i64) -> Msg.., i64 { to_far(k, 1); return Msg.Ping, 0; }

fn main() {
    for k in 2 {
        let a, b = widened(k);
        let c, d = floats(k);
        print(a, " ", b, " ", c, " ", d);           // -5 -6 1099511627776 -7, 200 -300 1.5 2.25
    }
    let p, x = pointees();
    let s, o = views();
    gp.a = 10;
    gx = 6;
    print(p, " ", x, " ", s, " ", o);               // P { 1, 2 } 5 abcd 6
    for k in 2 {
        let e, n = fixed(k);
        let v, m = varmode(k);
        print(e, " ", n, " ", v, " ", m);           // B { 3 } 4 B { 3 } 4, A { 9 } 10 B { 7 } 8
    }
    let i, l = later();
    let t, j = texts();
    print(i, " ", l, " ", t, " ", j);               // 5 A { 6 } Text { "hey" } 11
    for k in 3 {
        let w, q = values(k);
        print(w, " ", q);                           // V { [1, 2, 3] } 3, W { 12 } 13, X { "xs" } 14
    }
    let h, u = prefixed();
    print(h, " ", u, " ", u.len);                   // 1 abc 3
    for k in 4 {
        let f, z = flat(k);
        print(f, " ", z);                           // hij 2, defg 3, ij 4, lm 5
    }
    for k in 2 {
        var g, y = growing(k);
        g.push('!');
        var r, c = shrinking(k);
        r.push(0);
        print(g, " ", y, " ", r, " ", c);           // hij! 2 [7, 8, 9, 0] 6, ij! 4 [4, 5, 0] 7
    }
    let li, lu = limited();
    print(li, " ", lu);                             // [1, 2, 3] xyz
    let first: Pick = Pick.First { k: 30 };
    let second: Pick = Pick.Second { k: 40 };
    let p1, f1 = picked(first);
    let p2, f2 = picked(second);
    print(p1, " ", f1, " ", p2, " ", f2);           // 30 -1 -2 40

    let ot, on = own_texts();
    let ov, om = own_values();
    print(ot, " ", on, " ", ov, " ", om);           // Text { "hey" } 21 V { [1, 2, 4] } 4
    for k in [3, 7] {
        let a, b = early(k);
        let c, d = early_items(k);
        let e, f = far(k);
        print(a, " ", b, " ", c, " ", d, " ", e, " ", f);
        // Text { "hey" } 3 V { [1, 2, 3] } 3 Text { "hey" } 3
        // Text { "early" } 1 V { [9, 9] } 1 Text { "far" } 2
    }
}
